/*
 *  Copyright (C) 2009 Samer Abdallah
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <SWI-Stream.h>
#include <SWI-Prolog.h>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <lo/lo.h>

// ---------------------------------------------------------------------------

// Reimplementation of lo_server_thread to all calls to
// Prolog from the server thread.

typedef struct _my_server_thread {
	lo_server    s;
	pthread_t    thread;
	volatile int active;
	volatile int done;
} *my_server_thread;

int my_server_thread_start(my_server_thread st);
int my_server_thread_stop(my_server_thread st);
int my_server_thread_run(my_server_thread st, int timeout);
void my_server_thread_free(my_server_thread st);
my_server_thread my_server_thread_new(const char *port, lo_err_handler err_h);

// ---------------------------------------------------------------------------

// BLOB to hold a lo_address
static PL_blob_t addr_blob;

// BLOB to hold server thread
static PL_blob_t server_blob;

static predicate_t call3;

install_t install();

foreign_t mk_address( term_t host, term_t port, term_t addr); 
foreign_t is_address( term_t addr); 
foreign_t send_now( term_t addr, term_t msg, term_t args); 
foreign_t send_at( term_t addr, term_t msg, term_t args, term_t time); 
foreign_t send_timestamped( term_t addr, term_t msg, term_t args, term_t sec, term_t frac); 
foreign_t now( term_t sec, term_t frac);

// OSC server predicates
foreign_t mk_server( term_t port, term_t server);
foreign_t start_server( term_t server);
foreign_t stop_server( term_t server);
foreign_t add_handler( term_t server, term_t msg, term_t types, term_t handler);
foreign_t del_handler( term_t server, term_t msg, term_t types);
foreign_t run_server( term_t server);


// BLOB functions
int addr_release(atom_t a) {
	PL_blob_t *type;
	size_t    len;
	void *p=PL_blob_data(a,&len,&type);
	if (p) lo_address_free(*(lo_address *)p);
	return TRUE;
}

int addr_write(IOSTREAM *s, atom_t a, int flags) {
	PL_blob_t *type;
	size_t    len;
	lo_address *p=(lo_address *)PL_blob_data(a,&len,&type);
	if (p) {
		const char *host = lo_address_get_hostname(*p);
		const char *port = lo_address_get_port(*p);
		if (host!=NULL && port!=NULL) {
			Sfprintf(s,"osc_address<%s:%s>",host,port);
		} else {
			Sfprintf(s,"osc_address<invalid>");
		}
	}
	return TRUE;
}

int server_release(atom_t a) {
	PL_blob_t *type;
	size_t    len;
	void *p=PL_blob_data(a,&len,&type);
	if (p) my_server_thread_free(*(my_server_thread *)p);
	return TRUE;
}

int server_write(IOSTREAM *s, atom_t a, int flags) {
	PL_blob_t *type;
	size_t    len;
	my_server_thread *p=(my_server_thread *)PL_blob_data(a,&len,&type);
	if (p) {
		char *url=lo_server_get_url((*p)->s);
		Sfprintf(s,"osc_server<%s>",url);
		free(url);
	}
	return TRUE;
}

install_t install() { 
	PL_register_foreign("osc_now",        2, (void *)now, 0);
	PL_register_foreign("osc_mk_address", 3, (void *)mk_address, 0);
	PL_register_foreign("osc_is_address", 1, (void *)is_address, 0);
	PL_register_foreign("osc_send_now",   3, (void *)send_now, 0);
	PL_register_foreign("osc_send_at",    4, (void *)send_at, 0);
	PL_register_foreign("osc_mk_server",  2, (void *)mk_server, 0);
	PL_register_foreign("osc_start_server",  1, (void *)start_server, 0);
	PL_register_foreign("osc_stop_server",   1, (void *)stop_server, 0);
	PL_register_foreign("osc_run_server",    1, (void *)run_server, 0);
	PL_register_foreign("osc_add_method",   4, (void *)add_handler, 0);
	PL_register_foreign("osc_del_method",   3, (void *)del_handler, 0);

	addr_blob.magic = PL_BLOB_MAGIC;
	addr_blob.flags = PL_BLOB_UNIQUE;
	addr_blob.name = "osc_address";
	addr_blob.acquire = 0;
	addr_blob.release = addr_release;
	addr_blob.write   = addr_write;
	addr_blob.compare = 0; 

	server_blob.magic = PL_BLOB_MAGIC;
	server_blob.flags = PL_BLOB_UNIQUE;
	server_blob.name = "osc_server";
	server_blob.acquire = 0; 
	server_blob.release = server_release;
	server_blob.write   = server_write; 
	server_blob.compare = 0; 

	call3 = PL_predicate("call",3,"user");
}

// throws a Prolog exception to signal type error
static int type_error(term_t actual, const char *expected)
{ 
	term_t ex = PL_new_term_ref();

  PL_unify_term(ex, PL_FUNCTOR_CHARS, "error", 2,
		      PL_FUNCTOR_CHARS, "type_error", 2,
		        PL_CHARS, expected,
		        PL_TERM, actual,
		      PL_VARIABLE);

  return PL_raise_exception(ex);
}

static int osc_error(int errno, const char *errmsg, const char *msg)
{ 
	term_t ex = PL_new_term_ref();

  PL_unify_term(ex, PL_FUNCTOR_CHARS, "error", 1,
		      PL_FUNCTOR_CHARS, "osc_error", 3,
		        PL_INTEGER, errno,
		        PL_CHARS, errmsg,
		        PL_CHARS, msg==NULL ? "none" : msg);

  return PL_raise_exception(ex);
}


// put lo_address into a Prolog BLOB 
static int unify_addr(term_t addr,lo_address a) {
	return PL_unify_blob(addr, &a, sizeof(lo_address), &addr_blob); 
}

// get lo_address from BLOB
static int get_addr(term_t addr, lo_address *a)
{ 
	PL_blob_t *type;
	size_t    len;
	lo_address *a1;
  
	PL_get_blob(addr, (void **)&a1, &len, &type);
	if (type != &addr_blob) {
		return type_error(addr, "osc_address");
	} else {
		*a=*a1;
		return TRUE;
	}
} 

// put lo_address into a Prolog BLOB 
static int unify_server(term_t server,my_server_thread s) {
	return PL_unify_blob(server, &s, sizeof(my_server_thread), &server_blob); 
}

// get my_server_thread from BLOB
static int get_server(term_t server, my_server_thread *a)
{ 
	PL_blob_t *type;
	size_t    len;
	my_server_thread *a1;
  
	PL_get_blob(server, (void **)&a1, &len, &type);
	if (type != &server_blob) {
		return type_error(server, "osc_server");
	} else {
		*a=*a1;
		return TRUE;
	}
} 

// get Prolog (Unix) time value and convert to OSC timestamp
static int get_prolog_time(term_t time, lo_timetag *ts) {
	double t, ft;
	int 	ok = PL_get_float(time, &t);

	ft=floor(t);
	ts->sec = ((uint32_t)ft)+2208988800u;
	ts->frac = (uint32_t)(4294967296.0*(t-ft));
	return ok;
}

static int get_timetag(term_t sec, term_t frac, lo_timetag *ts) {
	int64_t	s, f;
	int 	ok = PL_get_int64(sec, &s) && PL_get_int64(frac, &f);
	ts->sec = s;
	ts->frac = f;
	return ok;
}


static int get_msg(term_t msg, char **m) {
	return PL_get_chars(msg, m, CVT_ATOM | CVT_STRING);
}

// parse a list of Prolog terms and add arguments to an OSC message 
static int add_msg_args(lo_message msg, term_t list)
{
	term_t 	head=PL_new_term_ref();

	// copy term ref so as not to modify original
	list=PL_copy_term_ref(list);

	while (PL_get_list(list,head,list)) {
		atom_t name;
		int	 arity;
		const char  *type;

		if (!PL_get_name_arity(head,&name,&arity)) return type_error(head,"term");
		type=PL_atom_chars(name);
		switch (arity) {
		case 1: {
				term_t a1=PL_new_term_ref();
				PL_get_arg(1,head,a1);

				if (!strcmp(type,"int")) {
					int x;
					if (!PL_get_integer(a1,&x)) return type_error(a1,"integer");
					lo_message_add_int32(msg,x);
				} else if (!strcmp(type,"double")) {
					double x;
					if (!PL_get_float(a1,&x)) return type_error(a1,"float");
					lo_message_add_double(msg,x);
				} else if (!strcmp(type,"string")) {
					char *x;
					if (!PL_get_chars(a1,&x,CVT_ATOM|CVT_STRING)) return type_error(a1,"string");
					lo_message_add_string(msg,x);
				} else if (!strcmp(type,"symbol")) {
					char *x;
					if (!PL_get_chars(a1,&x,CVT_ATOM)) return type_error(a1,"atom");
					lo_message_add_symbol(msg,x);
				} else if (!strcmp(type,"float")) {
					double x;
					if (!PL_get_float(a1,&x)) return type_error(a1,"float");
					lo_message_add_float(msg,(float)x);
				}
				break;
			}
		case 0: {
				if (!strcmp(type,"true")) lo_message_add_true(msg);
				else if (!strcmp(type,"false")) lo_message_add_false(msg);
				else if (!strcmp(type,"nil")) lo_message_add_nil(msg);
				else if (!strcmp(type,"inf")) lo_message_add_infinitum(msg);
				break;
			}
		}
	}
	if (!PL_get_nil(list)) return type_error(list,"nil");
	return TRUE;
}

static int send_msg_timestamped(lo_address a, lo_timetag *ts, char *path, term_t args)
{
	lo_message msg=lo_message_new();
	lo_bundle  bun=lo_bundle_new(*ts);

	if (add_msg_args(msg,args)) {
		int ret;

		lo_bundle_add_message(bun,path,msg);
		ret = lo_send_bundle(a,bun);
		lo_message_free(msg);
		lo_bundle_free(bun);
		if (ret==-1) {
			return osc_error(lo_address_errno(a),lo_address_errstr(a),path);
		} else {
			return TRUE;
		}
	} else return FALSE;
}

static int send_msg(lo_address a, char *path, term_t args)
{
	lo_message msg=lo_message_new();

	if (add_msg_args(msg,args)) {
		if (lo_send_message(a,path,msg)==-1) {
			lo_message_free(msg);
			return osc_error(lo_address_errno(a),lo_address_errstr(a),path);
		} else {
			lo_message_free(msg);
			return TRUE;
		}
	} else return FALSE;
}

foreign_t mk_address(term_t host, term_t port, term_t addr) { 
	char *h, *p;

	if (PL_get_chars(host, &h, CVT_ATOM | CVT_STRING)) {
		if (PL_get_chars(port, &p, CVT_INTEGER)) {
			lo_address a = lo_address_new(h,p);
			return unify_addr(addr,a);
		} else {
			return type_error(port,"integer");
		}
	} else {
		return type_error(host,"atom");
	}
}

foreign_t now(term_t sec, term_t frac) { 
	lo_timetag ts;
	int64_t s, f;

	lo_timetag_now(&ts);
	s=ts.sec; f=ts.frac;
	return PL_unify_int64(sec,s) && PL_unify_int64(frac,f);
}


// set current random state structure to values in Prolog term
foreign_t is_address(term_t addr) { 
	PL_blob_t *type;
	return PL_is_blob(addr,&type) && type==&addr_blob;
}


foreign_t send_at(term_t addr, term_t msg, term_t args, term_t time) {
	lo_address 	a;
	lo_timetag  ts;
	char			*m;

	return get_addr(addr,&a) &&
			get_prolog_time(time,&ts) &&
			get_msg(msg, &m) &&
			send_msg_timestamped(a,&ts,m,args);
}

foreign_t send_timestamped(term_t addr, term_t msg, term_t args, term_t secs, term_t frac) {
	lo_address 	a;
	lo_timetag  ts;
	char			*m;

	return get_addr(addr,&a) &&
			get_timetag(secs,frac,&ts) &&
			get_msg(msg, &m) &&
			send_msg_timestamped(a,&ts,m,args);
}



foreign_t send_now(term_t addr, term_t msg, term_t args) {
	lo_address 	a;
	char			*m;

	return get_addr(addr,&a) &&
			get_msg(msg, &m) &&
			send_msg(a,m,args);
}



/* 
 * Server Bits 
 */

static void prolog_thread_func(void *data);

// parse a list of type terms and encode as a NULL terminated
// string where each character encodes the type of one argument.
static int get_types_list(term_t list, char *typespec, int len)
{
	term_t 	head=PL_new_term_ref();
	int		count=0;

	// copy term ref so as not to modify original
	list=PL_copy_term_ref(list);

	while (PL_get_list(list,head,list) && count<len) {
		atom_t name;
		int	 arity;
		const char  *type;

		if (!PL_get_name_arity(head,&name,&arity)) return type_error(head,"term");
		type=PL_atom_chars(name);
		switch (arity) {
		case 1: {
				if (!strcmp(type,"int")) {
					typespec[count++]='i';
				} else if (!strcmp(type,"double")) {
					typespec[count++]='d';
				} else if (!strcmp(type,"string")) {
					typespec[count++]='s';
				} else if (!strcmp(type,"symbol")) {
					typespec[count++]='S';
				} else if (!strcmp(type,"float")) {
					typespec[count++]='f';
				}
				break;
			}
		case 0: {
				if (!strcmp(type,"true")) typespec[count++]='T';
				else if (!strcmp(type,"false")) typespec[count++]='F';
				else if (!strcmp(type,"nil")) typespec[count++]='N';
				else if (!strcmp(type,"inf")) typespec[count++]='I';
				break;
			}
		}
	}
	typespec[count]=0;
	if (!PL_get_nil(list)) return type_error(list,"nil");
	return TRUE;
}

// parse a term representing argument types - types can be a list
// as accepted by get_types_list() above or the atom 'any'
static int get_types(term_t types, char *buffer, int len, char **typespec)
{
	if (PL_is_list(types)) {
		*typespec=buffer;
		return get_types_list(types,buffer,len);
	} else if (PL_is_atom(types)) {
		char *a;
		PL_get_atom_chars(types,&a);
		if (strcmp(a,"any")==0) { *typespec=NULL; return TRUE; } 
		else return type_error(types,"list or 'any'");
	} else return type_error(types,"list or 'any'");
}

// handler server error
static void server_error(int num, const char *msg, const char *path) {
	osc_error(num,msg,path);
}

// handle the /plosc/stop message for the synchronous server loop
// in run_stoppable_server() and hence osc_run_server/1
static int stop_handler(const char *path, const char *types, lo_arg **argv,
		    int argc, lo_message msg, void *user_data) 
{
	my_server_thread s=(my_server_thread)user_data;
	s->active=0;
	return 1;
}

// handle OSC message by calling the associated Prolog goal
static int prolog_handler(const char *path, const char *types, lo_arg **argv,
		    int argc, lo_message msg, void *user_data) 
{
	term_t goal  = PL_new_term_ref();
	term_t term0 = PL_new_term_refs(3);
	term_t term1 = term0+1;
	term_t term2 = term0+2;
	term_t list;
	int 	i, rc=0;

	PL_recorded((record_t)user_data,goal); // retrieve the goal term
	PL_put_term(term0,goal); // term_t goal encoded in user_data
	PL_put_atom_chars(term1,path);

	list = PL_copy_term_ref(term2);
	for (i=0; i<argc; i++) {
		term_t head=PL_new_term_ref();
		term_t tail=PL_new_term_ref();
		if (!PL_unify_list(list,head,tail)) PL_fail; 
		switch (types[i]) {
			case 'c': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"char",1,PL_INT,(int)argv[i]->c); break;
			case 'i': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"int",1,PL_INT,argv[i]->i); break;
			case 'h': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"int64",1,PL_INT64,argv[i]->h); break;
			case 'f': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"float",1,PL_FLOAT,(double)argv[i]->f); break;
			case 'd': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"double",1,PL_DOUBLE,argv[i]->d); break;
			case 's': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"string",1,PL_CHARS,&argv[i]->s); break;
			case 'S': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"symbol",1,PL_CHARS,&argv[i]->S); break;
			case 'T': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"true",0); break;
			case 'F': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"false",0); break;
			case 'N': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"nil",0); break;
			case 'I': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"inf",0); break;
			case 'b': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"blob",0); break;
			case 't': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"timetag",2,
								PL_INT64,(int64_t)argv[i]->t.sec,
								PL_INT64,(int64_t)argv[i]->t.frac); 
						 break;
			case 'm': rc=PL_unify_term(head,PL_FUNCTOR_CHARS,"midi",4,
								PL_INT,(int)argv[i]->m[0], PL_INT,(int)argv[i]->m[1],
								PL_INT,(int)argv[i]->m[2], PL_INT,(int)argv[i]->m[3]);
						 break;
		}
		if (!rc) PL_fail;
		list=tail;
	}
	return PL_unify_nil(list) && PL_call_predicate(NULL,PL_Q_NORMAL,call3,term0);
}

/*
static int generic_handler(const char *path, const char *types, lo_arg **argv,
		    int argc, lo_message msg, void *user_data) 
{
	int i;

	printf("path: <%s>\n", path);
	for (i=0; i<argc; i++) {
		printf("arg %d '%c' ", i, types[i]);
		lo_arg_pp(types[i], argv[i]);
		printf("\n");
	}
	printf("\n");
	fflush(stdout);
	return 1;
}

static int verbose_prolog_handler(const char *path, const char *types, lo_arg **argv,
		    int argc, lo_message msg, void *user_data) 
{
	generic_handler(path,types,argv,argc,msg,user_data);
	prolog_handler(path,types,argv,argc,msg,user_data);
	return 1;
}
*/

// run OSC server in this thread but with an extra message handler
// to allow the /plosc/stop message to terminate the loop.
static int run_stoppable_server(my_server_thread s, int timeout)
{
	lo_server_add_method(s->s, "/plosc/stop", NULL, stop_handler, (void *)s);
	my_server_thread_run(s,timeout);
	lo_server_del_method(s->s,"/plosc/stop",NULL);
	return TRUE;
}

foreign_t mk_server(term_t port, term_t server)
{
	char *p;

	if (PL_get_chars(port, &p, CVT_INTEGER)) {
		my_server_thread s = my_server_thread_new(p, server_error);
		if (s) return unify_server(server,s);
		else return FALSE; 
	} else {
		return type_error(port,"integer");
	}
}

foreign_t add_handler(term_t server, term_t msg, term_t types, term_t goal)
{
	my_server_thread s;
	lo_method method;
	char	*pattern, *typespec;
	char	buffer[256]; // !! space for up to 255 arguments
	int	rc;

	rc = get_server(server,&s) 
		&& get_msg(msg,&pattern) 
		&& get_types(types,buffer,256,&typespec);

	if (rc) {
		record_t goal_record=PL_record(goal);
		method = lo_server_add_method(s->s, pattern, typespec, prolog_handler, (void *)goal_record);
	} 
	return rc;
}

foreign_t del_handler(term_t server, term_t msg, term_t types)
{
	my_server_thread s;
	char	*pattern, *typespec;
	char	buffer[256]; // !! space for up to 255 arguments
	int	rc;

	rc = get_server(server,&s) 
		&& get_msg(msg,&pattern) 
		&& get_types(types,buffer,256,&typespec);

	if (rc) lo_server_del_method(s->s,pattern,typespec);
	return rc;
}

foreign_t start_server( term_t server)
{
	my_server_thread s;
	return get_server(server,&s) && (my_server_thread_start(s)==0);
}

foreign_t stop_server( term_t server)
{
	my_server_thread s;
	return get_server(server,&s) && (my_server_thread_stop(s)==0);
}

foreign_t run_server( term_t server)
{
	my_server_thread s;
	printf("running OSC server synchronously...\n");
	return get_server(server,&s) && run_stoppable_server(s,10);
}


// -------------------------------------------------------------------------
// my_server_thread implementation

my_server_thread my_server_thread_new(const char *port, lo_err_handler err_h)
{
	my_server_thread st = malloc(sizeof(struct _my_server_thread));
	st->s = lo_server_new(port, err_h);
	st->active = 0;
	st->done = 0;

	if (!st->s) {
		free(st);
		return NULL;
	}
	return st;
}

void my_server_thread_free(my_server_thread st)
{
	if (st) {
		if (st->active) {
			my_server_thread_stop(st);
		}
		lo_server_free(st->s);
	}
	free(st);
}

int my_server_thread_stop(my_server_thread st)
{
	int result;

	if (st->active) {
		st->active = 0; // Signal thread to stop
	
		result = pthread_join( st->thread, NULL );
		if (result) {
			fprintf(stderr,"Failed to stop thread: pthread_join(), %s",strerror(result));
			return -result;
		}
	}

	return 0;
}


int my_server_thread_start(my_server_thread st)
{
	int result;

	if (!st->active) {
		st->active = 1;
		st->done = 0;

		// Create the server thread
		result = pthread_create(&(st->thread), NULL, (void *)&prolog_thread_func, st);
		if (result) {
			  fprintf(stderr, "Failed to create thread: pthread_create(), %s",
						 strerror(result));
			  return -result;
		}
	
	}
    return 0;
}

int my_server_thread_run(my_server_thread st, int timeout)
{
	st->active = 1;
	st->done = 0;
	while (st->active) {
		lo_server_recv_noblock(st->s, timeout);
	}
	st->done = 1;
	return 0;
}

// code for the asynchronous server loop
// we must create and attach a new Prolog engine to enable
// calls to Prolog from this thread.
static void prolog_thread_func(void *data)
{
	my_server_thread st = (my_server_thread)data;

	printf("OSC server started.\n");
	PL_thread_attach_engine(NULL);
	my_server_thread_run(st,10);
	PL_thread_destroy_engine(); 
	printf("OSC server stopped.\n");
	pthread_exit(NULL);
}

