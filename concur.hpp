#ifndef _CONCUR_HPP_
#define _CONCUR_HPP_

#include <ev.h>
#include <zmq.h>
#include <pthread.h>
#include <stdexcept>

#include <list>
namespace Concur {
  class Context;
  //
  // Base classes
  //
  class Socket {
      // c++ wrapper of zmq socket
      friend class Context;
    protected:
      // zmq socket
      void * _zmq_socket_;
      // zmq poller
      bool (*_zmq_poller_fp_)(void*, int);
      void * _zmq_pollitem_;
    public:
      Socket(Context*);
    protected:
      // zmq socket helper functions
      void open_zmq_socket(Context *,int);
      void close_zmq_socket(Context *);
      int get_zmq_events() {
        int zmq_events;
        size_t optlen = sizeof(zmq_events);
        int rc = zmq_getsockopt(_zmq_socket_, ZMQ_EVENTS, &zmq_events, &optlen);
        if (rc != 0)
          throw std::runtime_error(zmq_strerror(zmq_errno()));
        return zmq_events;
      }
      // zeromq functions
      virtual void start(Context * c){
        open_zmq_socket(c, ZMQ_PUSH);
      }
      virtual void stop(Context * c){
        close_zmq_socket(c);
      }
      // message functions
    protected:
      zmq_msg_t send_msg;
      zmq_msg_t msg;
    public:
      bool accept_events(int evts){
        return (*_zmq_poller_fp_)(this, evts);
      }
      void *message() {
        return zmq_msg_data(&msg);
      }

      const void* message() const {
        return zmq_msg_data(const_cast<zmq_msg_t*> (&msg));
      }

      int message_size() const {
        return zmq_msg_size(const_cast<zmq_msg_t*> (&msg));
      }
      void receive(int flgs = 0) {
        int rc = zmq_msg_init(&msg);
        if (rc != 0)
          throw std::runtime_error(zmq_strerror(zmq_errno()));
        rc = zmq_msg_recv(&msg, _zmq_socket_, flgs);
        if (rc < 0)
          throw std::runtime_error(zmq_strerror(zmq_errno()));
      }
      void init_send_message(size_t size_)
      {
          int rc = zmq_msg_init_size (&send_msg, size_);
          if (rc != 0)
            throw std::runtime_error(zmq_strerror(zmq_errno()));
      }
      void *send_message() {
        return zmq_msg_data(&send_msg);
      }

      bool send (int flgs = 0)
      {
          int nbytes = zmq_msg_send (&send_msg, _zmq_socket_, flgs);
          if (nbytes >= 0)
              return true;
          if (zmq_errno () == EAGAIN)
              return false;
          throw std::runtime_error(zmq_strerror(zmq_errno()));
      }
      virtual void connect (const char *addr)
      {
          int rc = zmq_connect (_zmq_socket_, addr);
          if (rc != 0)
            throw std::runtime_error(zmq_strerror(zmq_errno()));
      }

  };
  class Module {
    public:
      Module(Context*c);
      virtual ~Module() {
      }
      virtual bool entry() {
        return true;
      }
      virtual bool main() {
        return true;
      }
      virtual bool exit(){
        return true;
      }
  };
  class Context {
      friend class Socket;
      void *_zmq_context_;
      struct ev_loop * _event_loop_;
      pthread_t * _thread_;

      Context * parent;
      std::list<Context*> children;
      std::list<Module*> modules;
      std::list<Socket*> sockets;
      std::list<Socket*> poll_sockets;
      std::list<Socket*> evented_sockets;
    public:
      static Context & ROOT(){
        static Context rt(NULL);
        return rt;
      }
    private:
      Context(){}
      Context(Context const &);
      void operator=(Context const &);
      Context(Context *p) :
        _zmq_context_(NULL), _event_loop_(NULL), _thread_(NULL), parent(p) {
        if (parent == NULL) {
          // this is a root context
          _zmq_context_ = zmq_ctx_new();
        } else {
          _zmq_context_ = parent->_zmq_context_;
          // add itself to parent
          parent->children.push_back(this);
        }
      }
      void zmqpoll_run(){
        int num_pollitems = poll_sockets.size();
        zmq_pollitem_t * items = new zmq_pollitem_t[num_pollitems];
        int i = 0;
        for (std::list<Socket*>::iterator it = poll_sockets.begin(); it!= poll_sockets.end(); it++){
          items[i].socket = (*it)->_zmq_socket_;
          items[i].events = ZMQ_POLLIN;
          (*it)->_zmq_pollitem_ = &items[i];
          i++;
        }
        bool break_out = false;
        while(!break_out){
          zmq_poll(items, num_pollitems, -1);
          for (std::list<Socket*>::iterator it = poll_sockets.begin(); it!= poll_sockets.end(); it++){
            int revts = reinterpret_cast<zmq_pollitem_t*>((*it)->_zmq_pollitem_)->revents;
            if (revts & ZMQ_POLLIN){
              if (!(*((*it)->_zmq_poller_fp_))(*it, revts)){
                break_out = true;
                break;
              }
            }
          }
        }
        // out of polling loop
        delete[] items;
      }
    public:
      Context * spawn() {
        return new Context(this);
      }
      ~Context() {
        if ((parent == NULL) && _zmq_context_)
          zmq_ctx_destroy(_zmq_context_);
      }
      void add_socket(Socket * s){
        sockets.push_back(s);
      }
      void add_poll_socket(Socket * s){
        poll_sockets.push_back(s);
      }
      void add_evented_socket(Socket * s){
        evented_sockets.push_back(s);
      }
      void add_module(Module*m) {
        modules.push_back(m);
      }
      struct ev_loop * event_loop(){
          return _event_loop_;
      }

      // context initializations
      void init_process(pthread_t * p = NULL) {
        _thread_ = p;
      }
      void enter_main(){
        // now the thread has started, do something before calling modules' main functions
        for (std::list<Socket*>::iterator it = sockets.begin(); it!= sockets.end(); it++){
          // init sockets, create zmq socket and setup event watchers for event loop
          (*it)->start(this);
        }
      }
      void exit_main(){
        // program wants to terminate current thread, clean sockets before exit
        for (std::list<Socket*>::iterator it = sockets.begin(); it!= sockets.end(); it++){
          (*it)->stop(this);
        }
      }
      // process controlled callback functions
      void entry() {
        // create event loop if there are evented sockets
        if(!evented_sockets.empty()){
          if (_thread_ == NULL) {
            // main or init thread
            _event_loop_ = ev_default_loop(EVBACKEND_EPOLL | EVFLAG_NOENV);
            if (!_event_loop_)
              throw std::runtime_error("ev_default_loop error");
          } else {
            _event_loop_ = ev_loop_new(EVBACKEND_EPOLL | EVFLAG_NOENV);
            if (!_event_loop_)
              throw std::runtime_error("ev_loop_new error");
          }
        }
        // Run in main thread, right before the thread starts
        for (std::list<Module*>::iterator it = modules.begin(); it
            != modules.end(); it++)
          (*it)->entry();
      }
      void main() {
        // Run inside the thread
        enter_main();
        // call module main functions
        for (std::list<Module*>::iterator it = modules.begin(); it!= modules.end(); it++){
          if (!(*it)->main()){
            // if module main return false to exit main
            exit_main();
            break;
          }
        }
        // block thread with event loop or zmq poller
        if (!evented_sockets.empty()){
          // start event loop
          ev_run(_event_loop_,0);
        }
        else if (!poll_sockets.empty()){
          // or zmq poller
          zmqpoll_run();
        }
      }
      void exit() {
        // Run in main thread, right after the thread ends
        for (std::list<Module*>::iterator it = modules.begin(); it
            != modules.end(); it++)
          (*it)->exit();
      }
  };
  inline void Socket::open_zmq_socket(Context *context, int type){
    _zmq_socket_ = zmq_socket(context->_zmq_context_, type);
    if (_zmq_socket_ == NULL)
      throw std::runtime_error(zmq_strerror(zmq_errno()));
  }
  inline void Socket::close_zmq_socket(Context *context){
    int rc = zmq_close (_zmq_socket_);
    if (rc != 0)
      throw std::runtime_error(zmq_strerror(zmq_errno()));
  }
  inline Socket::Socket(Context * c): _zmq_socket_(NULL), _zmq_poller_fp_(NULL), _zmq_pollitem_(NULL){
    c->add_socket(this);
  }
  inline Module::Module(Context*c) {
    c->add_module(this);
  }

  class Process {
      static std::list<Process*> processes;
      const bool is_thread;
    protected:
      Context * context;
      friend class Context;
    public:
      Process(Context * c, bool is_thrd=true) :
        context(c), is_thread(is_thrd) {
        if (is_thread)
          processes.push_back(this);
      }
      virtual void start() = 0;
      virtual void stop() = 0;
      static void start_all() {
        for (std::list<Process*>::iterator it = processes.begin(); it
            != processes.end(); it++)
          (*it)->start();
      }
      static void stop_all() {
        for (std::list<Process*>::iterator it = processes.begin(); it
            != processes.end(); it++)
          (*it)->stop();
      }
  };
  class Proc: public Process {
      // wrapper of pthread
      pthread_t process;
      static void* run(void*opq) {
        Context* obj = reinterpret_cast<Context*> (opq);
        // initialize thread task before task run
        // e.g. start event loop here if there is a valid one
        // task run
        obj->main();
        return obj;
      }
    public:
      Proc(Context * c) :
        Process(c) {
        context->init_process(&process);
      }
      void start() {
        context->entry();
        int rc = pthread_create(&process, NULL, run, context);
        if (rc != 0) {
          throw std::runtime_error("pthread create error");
        }
      }
      void stop() {
        int rc = pthread_join(process, NULL);
        if (rc != 0) {
          throw std::runtime_error("pthread join error");
        }
        context->exit();
      }
  };
  //
  // Extensions
  //
  class InSocket : public Socket{
    public:
      InSocket(Context * c):Socket(c){
      }
      virtual void start(Context * c){
        open_zmq_socket(c, ZMQ_PULL);
      }
      void connect (const char *addr)
      {
          int rc = zmq_bind (_zmq_socket_, addr);
          if (rc != 0)
            throw std::runtime_error(zmq_strerror(zmq_errno()));
      }
  };

  template<typename T>
  class PollerSocket: public InSocket {
    Context * context;
    typedef PollerSocket<T> tPollerSocket;
    static bool pollin_cb(void* opq, int revents){
      tPollerSocket * s = reinterpret_cast<tPollerSocket*> (opq);
      if (revents & ZMQ_POLLIN){
        s->receive();
        if(!s->object->accept_events(s, revents)){
          s->context->exit_main();
          return false;
        }
      }
      return true;
    }
    T * object;
    public:
    PollerSocket(Context * c, T*t):InSocket(c), context(c),object(t){
      _zmq_poller_fp_ = &pollin_cb,
      c->add_poll_socket(this);
    }
  };

  template<typename T>
  class EventedSocket: public InSocket {
      typedef EventedSocket<T> tEventedSocket;
    private:
      uint32_t trigers;
      Context * context;
      T * object;

      int get_events() {
        int events = 0;
        int zmq_events = get_zmq_events();
        if (zmq_events & ZMQ_POLLOUT)
          events |= trigers & EV_WRITE;
        if (zmq_events & ZMQ_POLLIN)
          events |= trigers & EV_READ;
        return events;
      }
      ev_prepare prep_watcher;
      ev_check chk_watcher;
      ev_idle idle_watcher;
      ev_io io_watcher;
      // Callbacks
      static void on_idle(struct ev_loop *loop, ev_idle *w, int revents) {
        //printf("%s\n",__PRETTY_FUNCTION__);
      }

      static void on_io(struct ev_loop *loop, ev_io *w, int revents) {
        //printf("%s\n",__PRETTY_FUNCTION__);
      }

      static void on_prepare(struct ev_loop *loop, ev_prepare *w, int revents) {
        tEventedSocket * s = reinterpret_cast<tEventedSocket*> (w->data);
        if (s->get_events()) {
          // idle ensures that libev will not block
          ev_idle_start(loop, &s->idle_watcher);
        }
      }

      static void on_check(struct ev_loop *loop, ev_check *w, int revents) {
        tEventedSocket * s = reinterpret_cast<tEventedSocket*> (w->data);
        ev_idle_stop(loop, &s->idle_watcher);
        int events = s->get_events();
        if (events) {
          // call desired event callback
          s->receive();
          if(!s->object->accept_events(s, events)){
            s->context->exit_main();
          }
        }
      }
    public:
      EventedSocket(Context * c, T*t, uint32_t trig = EV_READ) :
        InSocket(c), trigers(trig), context(c), object(t) {
        c->add_evented_socket(this);
        prep_watcher.data = this;
        chk_watcher.data = this;
        idle_watcher.data = this;
        io_watcher.data = this;
      }
      void start(Context *c){
        InSocket::start(c);

        // initialize event watchers
        ev_prepare_init(&prep_watcher, on_prepare);
        ev_check_init(&chk_watcher, on_check);
        ev_idle_init(&idle_watcher, on_idle);
        int fd;
        size_t optlen = sizeof(fd);
        int rc = zmq_getsockopt(_zmq_socket_, ZMQ_FD, &fd, &optlen);
        if (rc != 0)
          throw std::runtime_error(zmq_strerror(zmq_errno()));
        ev_io_init(&io_watcher, on_io, fd, trigers ? EV_READ : 0);
        struct ev_loop * loop = c->event_loop();
        if (loop){
          ev_prepare_start(loop, &prep_watcher);
          ev_check_start(loop, &chk_watcher);
          ev_io_start(loop, &io_watcher);
        }
      }
      void stop(Context * c) {
        // stop event watchers
        struct ev_loop * loop = c->event_loop();
        if (loop){
          ev_prepare_stop(loop, &prep_watcher);
          ev_check_stop(loop, &chk_watcher);
          ev_idle_stop(loop, &idle_watcher);
          ev_io_stop(loop, &io_watcher);
        }
        // stop zmq socket
        InSocket::stop(c);
      }
  };
  class MainProc: public Process {
    public:
      MainProc(Context * c) :
        Process(c) {
        context->init_process();
      }
      void start() {
        context->entry();
      }
      void stop() {
        context->exit();
      }
  };
}

#endif // _CONCUR_HPP_
