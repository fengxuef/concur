#include "concur.hpp"
#include <string>
#include <sstream>
#include <iostream>
std::list<Concur::Process*> Concur::Process::processes;
static int num_msg = 1000000;
static int size_msg = 1024;
class Producer: public Concur::Module {
    Concur::Socket output;
  public:
    Producer(Concur::Context&c) :
      Concur::Module(&c), output(&c) {
    }
    bool entry(){
      //printf("%s\n",__PRETTY_FUNCTION__);
      return true;
    }
    bool main(){
      std::string text("hhahaha");
      output.connect("inproc://thr_test");
      for (int i = 0; i< num_msg;i++){
        //printf("%d : %s\n",i, __PRETTY_FUNCTION__);
        output.init_send_message(size_msg);
        memcpy(output.send_message(), text.c_str(),text.size());
        output.send();
      }
      return false;
    }
    bool exit(){
      //printf("%s\n",__PRETTY_FUNCTION__);
      return true;
    }
};
class LoopWorker: public Concur::Module {
    Concur::Socket output;
    Concur::InSocket input;
  public:
    LoopWorker(Concur::Context&c) :
      Concur::Module(&c), output(&c), input(&c) {
    }
    bool main(){
      input.connect("inproc://thr_test");
      for (int i = 0; i< num_msg;i++){
        //printf("%d : %s\n",i, __PRETTY_FUNCTION__);
        input.receive();
        //printf("%d : %s received:%d %s\n",i, __PRETTY_FUNCTION__, input.message_size(), input.message());
      }
      return false;
    }
};
class ZPollWorker: public Concur::Module {
  Concur::Socket output;
  Concur::PollerSocket<ZPollWorker> input;
  int msg_count;
public:
  ZPollWorker(Concur::Context&c) :
    Concur::Module(&c), output(&c), input(&c, this), msg_count(0){
  }
  bool accept_events(Concur::PollerSocket<ZPollWorker>*s, uint32_t evts) {
    if (s == &input) {
      // to receive msg form input socket
      //printf("%d : %s received:%d %s\n",msg_count, __PRETTY_FUNCTION__, input.message_size(), input.message());
      msg_count ++;
      if (msg_count >= num_msg) return false;
    }
    return true;
  }
  bool main(){
    input.connect("inproc://thr_test");
    return true;
  }
};

class EventedWorker: public Concur::Module {
    Concur::Socket output;
    Concur::EventedSocket<EventedWorker> input;
    int msg_count;
  public:
    EventedWorker(Concur::Context&c) :
      Concur::Module(&c), output(&c), input(&c, this), msg_count(0){
    }
    bool accept_events(Concur::EventedSocket<EventedWorker>*s, uint32_t evts) {
      if (s == &input) {
        // to receive msg form input socket
        //printf("%d : %s received:%d %s\n",msg_count, __PRETTY_FUNCTION__, input.message_size(), input.message());
        msg_count ++;
        if (msg_count >= num_msg) return false;
      }
      return true;
    }
    bool entry(){
      //printf("%s\n",__PRETTY_FUNCTION__);
      return true;
    }
    bool main(){
      input.connect("inproc://thr_test");
      return true;
    }
    bool exit(){
      //printf("%s\n",__PRETTY_FUNCTION__);
      return true;
    }
};
int main (int argc, char *argv []) {
  if (argc != 4) {
      printf ("usage: thread_thr <mode> <message-size> <message-count>\n");
      return 1;
  }

  int mode = atoi (argv [1]);
  size_msg = atoi (argv [2]);
  num_msg = atoi (argv [3]);

  void * watch = zmq_stopwatch_start ();

  // contexts
  Concur::Context * c0 = Concur::Context::ROOT().spawn();
  Concur::Context * c1 = Concur::Context::ROOT().spawn();

  Concur::Proc p0(c0);
  Concur::Proc p1(c1);

  Producer p(*c0);

  Concur::Module* w0;
  Concur::Module*  w1;
  Concur::Module*  w2;
  switch(mode){
  case 0:
    w0 = new LoopWorker(*c1);
    break;
  case 1:
    w1 = new ZPollWorker(*c1);
    break;
  case 2:
  default:
    w2 = new EventedWorker(*c1);
    break;
  }
  //LoopWorker w0(*c1);
  //ZPollWorker w0(*c1);
  //EventedWorker w0(*c1);
  // processes
  //Concur::MainProc main(&root);

  Concur::Process::start_all();
  //Concur::MainProc
  // wait here
  Concur::Process::stop_all();
  switch(mode){
  case 0:
    printf ("Loop Worker: %d %d\n", size_msg, num_msg);
    delete w0;
    break;
  case 1:
    printf ("ZPoller Worker: %d %d\n", size_msg, num_msg);
    delete w1;
    break;
  case 2:
  default:
    printf ("Evented Worker: %d %d\n", size_msg, num_msg);
    delete w2;
    break;
  }
  delete c1;
  delete c0;

  unsigned long elapsed = zmq_stopwatch_stop (watch);

  if (elapsed == 0)
      elapsed = 1;
  unsigned long throughput = (unsigned long)
      ((double) num_msg / (double) elapsed * 1000000);
  double megabits = (double) (throughput * size_msg * 8) / 1000000;

  printf ("mean throughput: %d [msg/s]\n", (int) throughput);
  printf ("mean throughput: %.3f [Mb/s]\n", (double) megabits);
}
