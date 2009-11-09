/*
 * Prolog library for sending and receiving OSC messages
 * Samer Abdallah (2009)
*/
	  
:- module(plosc, [
		osc_now/2			% -Seconds:int, -Fraction:int
	,	osc_mk_address/3	% +Host:atom, +Port:nonneg, -Ref
	,	osc_is_address/1  % +Ref
	,	osc_send/3			% +Ref, +Path:atom, +Args:list(OSCArg)
	,	osc_send/4			% +Ref, +Path:atom, +Args:list(OSCArg), +Time:float
	,	osc_mk_server/2	% +Port:nonneg, -Ref
	,	osc_start_server/1 % +Ref
	,	osc_stop_server/1  % +Ref
	,	osc_run_server/1   % +Ref
	,	osc_add_handler/4  % +Ref, +Path:atom, +Types:list(OSCArg), +Goal:callable
	,	osc_del_handler/3  % +Ref, +Path:atom, +Types:list(OSCArg)
	]).
	
:- meta_predicate osc_add_handler(+,+,+,2).

:-	load_foreign_library(foreign(plosc)).

%% osc_mk_address(+Host:atom, +Port:nonneg, -Ref) is det.
%
%  Construct a BLOB atom representing an OSC destination.
%
%  @param Host is the hostname or IP address of the OSC receiver
%  @param Port is the port number of the OSC receiver
%  @param Ref is an atom representing the address

%% osc_is_address(+Ref) is semidet.
%
%  Succeeds if Ref is an OSC address created by osc_mk_address/2

%% osc_send(+Ref, +Path:atom, +Args:list(OSCArg)) is det.
%% osc_send(+Ref, +Path:atom, +Args:list(OSCArg), +Time:float) is det.
%
%  Sends an OSC message scheduled for immediate execution (osc_send/3) or
%  at a given time (osc_send/4).
%
%  @param Ref is an OSC address BLOB as returned by osc_mk_address.
%  @param Path is an atom representing the OSC message path, eg '/foo/bar'
%  @param Args is a list of OSC message arguments, which can be any of:
%  	* string(+X:text)
%  	String as atom or Prolog string
%  	* symbol(+X:atom)
%  	* double(+X:float)
%  	Double precision floating point
%  	* float(+X:float)
%  	Single precision floating point
%  	* int(+X:integer)
%  	* true
%  	* false
%  	* nil
%  	* inf
%
osc_send(A,B,C) :- osc_send_now(A,B,C).
osc_send(A,B,C,T) :- T1 is T, osc_send_at(A,B,C,T1).


%% osc_mk_server(+Port:nonneg, -Ref) is det.
%
%  Create an OSC server and return a BLOB atom representing it.
%
%  @param Port is the port number of the OSC server
%  @param Ref is an atom representing the server

%% osc_start_server(+Ref) is det.
%
%  Run the OSC server referred to by Ref in a new thread. The new thread
%  dispatches OSC messages received to the appropriate handlers as registered
%  using osc_add_handler/4.

%% osc_stop_server(+Ref) is det.
%
%  If Ref refers to a running server thread, stop the thread.

%% osc_run_server(+Ref) is det.
%
%  The OSC server is run in the current thread, and does not return until
%  the message loop terminates. This can be triggered by sending the
%  message /plosc/stop to the server. Using this synchronous server
%  avoids creating a new thread and a new Prolog engine.

%% osc_add_handler( +Ref, +Path:atom, +Types:list(OSCArg), +Goal:callable) is det.
%
%  This registers a callable goal to handle the specified message Path for the
%  OSC server referred to by Ref.
%
%  @param Types is a list of terms specifying the argument types that this handler
%               will match. The terms are just like those descibed in osc_send/3
%               and osc_send/4, except that the actual values are not used and
%               can be left as anonymous variables, eg [int(_),string(_)].
%               Alternatively, Types can be the atom 'any', which will match any
%               arguments.
%
%  @param Goal  is any term which can be called with call/3 with two further
%               arguments, which will be the message Path and the argument list, eg
%               call( Goal, '/foo', [int(45),string(bar)]).

%% osc_del_handler( +Ref, +Path:atom, +Types:list(OSCArg) is det.
%
%  Deregister a message handler previously registered with osc_add_handler/4.


osc_add_handler(Ref,Path,Types,Goal) :- osc_add_method(Ref,Path,Types,Goal).
osc_del_handler(Ref,Path,Types)      :- osc_del_method(Ref,Path,Types).


prolog:message(error(osc_error(Num,Msg,Path)), ['LIBLO error ~w: ~w [~w]'-[Num,Msg,Path] |Z],Z).
