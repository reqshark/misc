/* demonstrate usage of epoll-based pcap application */

#define _GNU_SOURCE
#include <errno.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pcap.h>

struct {
  int verbose;
  char *prog;
  char *dev;
  char *filter;
  int pcap_fd;
  pcap_t *pcap;
  struct bpf_program fp;
  char err[PCAP_ERRBUF_SIZE];
  int snaplen;
  int ticks;
  int signal_fd;
  int epoll_fd;
} cfg = {
  .snaplen = 65535,
  .pcap_fd = -1,
  .dev = "eth0",
};

void usage() {
  fprintf(stderr,"usage: %s [-v] -f <bpf-filter>                \n"
                 "               -i <eth>   (read from interface)\n"
                 "\n",
          cfg.prog);
  exit(-1);
}

/* signals that we'll accept via signalfd in epoll */
int sigs[] = {SIGHUP,SIGTERM,SIGINT,SIGQUIT,SIGALRM};

void periodic_work() {
}

void cb(u_char *data, const struct pcap_pkthdr *hdr, const u_char *pkt) {
  if (cfg.verbose) fprintf(stderr,"packet of length %d\n", hdr->len);
}

int set_filter() {
  if (cfg.filter == NULL) return 0;

  int rc=-1;
  if ( (rc = pcap_compile(cfg.pcap, &cfg.fp, cfg.filter, 0, PCAP_NETMASK_UNKNOWN)) != 0) {
    fprintf(stderr, "error in filter expression: %s\n", cfg.err);
    goto done;
  }
  if ( (rc = pcap_setfilter(cfg.pcap, &cfg.fp)) != 0) {
    fprintf(stderr, "can't set filter expression: %s\n", cfg.err);
    goto done;
  }
  rc=0;

 done:
  return rc;
}

void do_stats(void) {
  struct pcap_stat ps;
  if (cfg.verbose == 0 ) return;
  if (pcap_stats(cfg.pcap,&ps)<0) {fprintf(stderr,"pcap_stat error\n"); return;}
  fprintf(stderr,"received : %u\n", ps.ps_recv);
  fprintf(stderr,"dropped: %u\n", ps.ps_drop);
}

int new_epoll(int events, int fd) {
  int rc;
  struct epoll_event ev;
  memset(&ev,0,sizeof(ev)); // placate valgrind
  ev.events = events;
  ev.data.fd= fd;
  if (cfg.verbose) fprintf(stderr,"adding fd %d to epoll\n", fd);
  rc = epoll_ctl(cfg.epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  if (rc == -1) {
    fprintf(stderr,"epoll_ctl: %s\n", strerror(errno));
  }
  return rc;
}

int handle_signal() {
  int rc=-1;
  struct signalfd_siginfo info;
  
  if (read(cfg.signal_fd, &info, sizeof(info)) != sizeof(info)) {
    fprintf(stderr,"failed to read signal fd buffer\n");
    goto done;
  }

  switch(info.ssi_signo) {
    case SIGALRM: 
      periodic_work();
      if ((++cfg.ticks % 10) == 0) do_stats();
      alarm(1); 
      break;
    default: 
      fprintf(stderr,"got signal %d\n", info.ssi_signo);  
      goto done;
      break;
  }

 rc = 0;

 done:
  return rc;
}

int get_pcap_data() {
  int rc=-1;

  if (pcap_dispatch(cfg.pcap, 10000,cb,NULL) < 0) {
    pcap_perror(cfg.pcap, "pcap error: "); 
    goto done;
  }
  rc = 0;

 done:
  return rc;
}

int main(int argc, char *argv[]) {
  struct epoll_event ev;
  cfg.prog = argv[0];
  int n,opt;

  while ( (opt=getopt(argc,argv,"vf:i:h")) != -1) {
    switch(opt) {
      case 'v': cfg.verbose++; break;
      case 'f': cfg.filter=strdup(optarg); break; 
      case 'i': cfg.dev=strdup(optarg); break; 
      case 'h': default: usage(); break;
    }
  }

  /* block all signals. we take signals synchronously via signalfd */
  sigset_t all;
  sigfillset(&all);
  sigprocmask(SIG_SETMASK,&all,NULL);

  /* a few signals we'll accept via our signalfd */
  sigset_t sw;
  sigemptyset(&sw);
  for(n=0; n < sizeof(sigs)/sizeof(*sigs); n++) sigaddset(&sw, sigs[n]);

  /* create the signalfd for receiving signals */
  cfg.signal_fd = signalfd(-1, &sw, 0);
  if (cfg.signal_fd == -1) {
    fprintf(stderr,"signalfd: %s\n", strerror(errno));
    goto done;
  }

  /* set up the epoll instance */
  cfg.epoll_fd = epoll_create(1); 
  if (cfg.epoll_fd == -1) {
    fprintf(stderr,"epoll: %s\n", strerror(errno));
    goto done;
  }

  /* add descriptors of interest */
  if (new_epoll(EPOLLIN, cfg.signal_fd))   goto done; // signal socket

  /* open capture interface and get underlying descriptor */
  cfg.pcap = pcap_open_live(cfg.dev, cfg.snaplen, 1, 0, cfg.err);
  if (!cfg.pcap) {fprintf(stderr,"can't open %s: %s\n",cfg.dev,cfg.err); goto done;}
  cfg.pcap_fd = pcap_get_selectable_fd(cfg.pcap);
  set_filter();
  if (new_epoll(EPOLLIN, cfg.pcap_fd)) goto done;

  alarm(1);
  while (epoll_wait(cfg.epoll_fd, &ev, 1, -1) > 0) {
    if (cfg.verbose > 1)  fprintf(stderr,"epoll reports fd %d\n", ev.data.fd);
    if      (ev.data.fd == cfg.signal_fd)   { if (handle_signal() < 0) goto done; }
    else if (ev.data.fd == cfg.pcap_fd)     { if (get_pcap_data() < 0) goto done; }
  }

done:
  if (cfg.pcap) pcap_close(cfg.pcap);
  if (cfg.pcap_fd > 0) close(cfg.pcap_fd);
  return 0;
}
