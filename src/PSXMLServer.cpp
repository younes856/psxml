#include "PSXMLServer.h"
#include <libxml++/libxml++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <cassert>
#include <netinet/in.h>


using namespace std;
using namespace psxml;
using namespace boost;
using namespace xmlpp;
using namespace Glib;

PSXMLServer::PSXMLServer(uint16_t port) {
  _fd = socket(AF_INET, SOCK_STREAM,0);
  assert(_fd > 0);
  _max_fd=_fd;
  sockaddr_in addr = { AF_INET, htons(port), {INADDR_ANY} };
  int b = bind(_fd, reinterpret_cast<const sockaddr*>(&addr),
    sizeof(sockaddr_in));
  assert(b==0);
  assert(listen(_fd,1024)==0);
  FD_ZERO(&_read);
  FD_ZERO(&_write);
  FD_ZERO(&_exception);
}
void PSXMLServer::run() {
  int ret=0;
  while(true) {
    _deal_with_sockets();
    // wait until something changes
    ret = select(_max_fd+1,&_read,&_write,&_exception,NULL);
    assert(ret >=0);
  }
}
void PSXMLServer::_deal_with_sockets() {

  // 1) check for errors
  for(map<int,PSXMLProtocol*>::iterator it = _protocols.begin();
    it != _protocols.end(); it++) {
    if(FD_ISSET(it->first,&_exception) != 0) {
      // something bad happended!
      // TODO: log
      _remove_fd(it->first);
    }
  }
  // 2) see if there are any awaiting new sockets on the maseter
  // sever socket
  if(FD_ISSET(_fd,&_read)!=0) {
    int new_fd = accept(_fd,NULL,NULL);
    assert(new_fd > 0);
    _update_max_fd(new_fd);
    _protocols[new_fd] = new PSXMLProtocol;
  }

  // 3) if there is data to be read, read and process (decode)
  for(map<int,PSXMLProtocol*>::iterator it = _protocols.begin();
    it != _protocols.end(); it++) {
    if(FD_ISSET(it->first,&_read) != 0) {
      // we have data (of some sort)
      // I assume that we'll never get over 64K worth
      // of data in one bunch
      char data[1024*64];
      ssize_t rs = recv(it->first,data,1024*64,0);
      assert(rs >= 0);
      if(rs == 0) {
        _remove_fd(it->first);
      } else {
        _route_xml(it->first,it->second->decode(data,rs));
      }
    }
  }
  // 4) if there is data to be written to, do so (encode)
  for(map<int,PSXMLProtocol*>::iterator it = _protocols.begin();
    it != _protocols.end(); it++) {
    // if there is work to be done
    if(FD_ISSET(it->first,&_write) != 0 
      && it->second->pull_encoded_size() > 0) {
      // we have data to send (of some sort)
      ssize_t ss = it->second->pull_encoded_size();
      ssize_t ss_ret = send(it->first,it->second->pull_encoded(),
        ss,MSG_DONTWAIT);
      // if there is too much data, throttle back
      while(ss_ret < 0) {
        // reduce the amount we attempt to send by half
        ss /= 2;
	assert (ss != 0);
        ss_ret = send(it->first,it->second->pull_encoded(),
          ss,MSG_DONTWAIT);
      }
      // tell the PSXML protocol manager that we did send
      // ss_ret bytes out
      it->second->pull_encoded(ss_ret);
    } // end "if we have data"
  } // end loop
  // 5) reset the read and write fds
  FD_ZERO(&_read); FD_ZERO(&_write); FD_ZERO(&_exception);
  FD_SET(_fd,&_read);
  for(map<int,PSXMLProtocol*>::iterator it = _protocols.begin();
    it != _protocols.end(); it++) {
    FD_SET(it->first,&_read);
    FD_SET(it->first,&_exception);
    if(it->second->pull_encoded_size() > 0)
      FD_SET(it->first,&_write);
  }

} /* end function */

void PSXMLServer::_update_max_fd(int fd) {
  if (fd > _max_fd)
    _max_fd = fd;
}

void PSXMLServer::_update_max_fd() {
  _max_fd = _fd;
  for(map<int,PSXMLProtocol*>::iterator it = _protocols.begin();
    it != _protocols.end(); it++) {
    _update_max_fd(it->first); 
  }
}
void PSXMLServer::_route_xml(int fd,vector<shared_ptr<Document> > docs) {
  Node::PrefixNsMap pnm;
  pnm["psxml"]="http://www.psxml.org/PSXML-0.1";
  for(unsigned int i = 0; i < docs.size(); i++) {
    shared_ptr<Document> doc = docs[i];
    Element * root = doc->get_root_node();
    if(root->get_name() == "Subscribe") {
      NodeSet subs = doc->get_root_node()->find(
        "/psxml:Subscribe/psxml:XPath",pnm);
      list<XPathExpression> exps;
      for(unsigned int i =0; i < subs.size(); i++) {
        Element * sub = dynamic_cast<Element*>(subs[i]);
        assert(sub != NULL);
        ustring exp(sub->get_attribute("exp")->get_value());
        NodeSet nss = sub->find("./psxml:Namespace",pnm);
        Node::PrefixNsMap prefix_map;
        XPathExpression xpath;
        xpath.expression = exp;
        for(unsigned int j = 0; j < nss.size(); j++) {
          Element * ns = dynamic_cast<Element*>(nss[i]);
          assert(ns != NULL);
          ustring pf(ns->get_attribute("prefix")->get_value());
          prefix_map[pf] = ns->get_child_text()->get_content();
        }
        xpath.ns = prefix_map;
	exps.push_back(xpath);
      }
      _engine.subscribe(fd,exps);
    }
    // find any data publishes
    _engine.publish( root->find("/psxml:Publish/*",pnm), _protocols);
  }
}

void PSXMLServer::_remove_fd(int fd) {
  delete _protocols[fd];
  _protocols.erase(fd);
  _update_max_fd();
}

