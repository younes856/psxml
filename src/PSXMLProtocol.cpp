#include "PSXMLProtocol.h"

#include <arpa/inet.h>
#include <cassert>
#include <cstring>

using namespace std;
using namespace psxml;
using namespace xmlpp;
using namespace boost;

// module local helper structure, PSXML Protocol's header frame
struct __psxml_header_t {
  char id[8];
  uint32_t size;
};

unsigned int PSXMLProtocol::_process_frame(const char * in,unsigned int size,
  vector<shared_ptr<Document> > & docs) {
  unsigned int read = 0;
  unsigned int offset = 0;
  // calculate any offset
  while(size - offset > 12 
    && string(in+offset,5)!="psxml") {
    offset++;
  }
  if(size-offset >= 12 && string(in+offset,5)=="psxml") {
    unsigned int payload_size = ntohl(
      *reinterpret_cast<const uint32_t*>(in+offset+8));
    if(size-offset-12 >= payload_size) {
      // do the xml parsing
      _parser.parse_memory_raw(reinterpret_cast<const unsigned char *>(
        in+offset+12),payload_size);
      // if the XML was parsed correctly, append it to the vector
      if(_parser) {
        shared_ptr<Document> doc(new Document());
        doc->create_root_node_by_import(
	  _parser.get_document()->get_root_node());
        docs.push_back(doc);
        // we just processed a whole bunch of bytes
        read = offset+12+payload_size;
      } else {
      }
    }
  }
  return read;
}

vector<shared_ptr<Document> > PSXMLProtocol::decode(const char * in,
  unsigned int size) {
  vector<shared_ptr<Document> > docs;
  // 1) Append start buffer to the end of the decder vector
  // make the vector big enough to handle the data
  unsigned int vector_size = _decoder_residual.size();
  _decoder_residual.resize(vector_size+size);
  //append
  memcpy(&_decoder_residual[0] + vector_size,in,size);
  // 2) call _process_frame
  const char * start = &_decoder_residual[0];
  unsigned int s = _decoder_residual.size();
  unsigned int p=0;
  unsigned int r = 0;
  do {
    r = _process_frame(start,s,docs);
    s-=r;
    start+=r;
    p+=r;
  } while(r!=0);

  // 3) reset the residual for future iterations
  memmove(&_decoder_residual[0], start, _decoder_residual.size()-p);
  _decoder_residual.resize(_decoder_residual.size()-p);
  // 4) we're done
  return docs;
}

unsigned int PSXMLProtocol::pull_encoded_size() const {
  return _encoder_residual.size();
}

const char * PSXMLProtocol::pull_encoded() {
  return &_encoder_residual[0];
}

void PSXMLProtocol::pull_encoded(unsigned int bytes) {
  // if bytes are > residual's size, something is wrong
  unsigned int s = _encoder_residual.size();
  assert(bytes <= s);
  memmove(&_encoder_residual[0], &_encoder_residual[0]+bytes,s-bytes);
  _encoder_residual.resize(s-bytes);
}

void PSXMLProtocol::encode(Document * in) {
  unsigned int s = _encoder_residual.size();
  Glib::ustring out = in->write_to_string();
  _encoder_residual.resize(s+out.bytes()+sizeof(__psxml_header_t));
  __psxml_header_t h = { "psxml",htonl(out.bytes()) };
  memcpy(&_encoder_residual[0]+s,&h,sizeof(__psxml_header_t));
  memcpy(&_encoder_residual[0]+s+sizeof(__psxml_header_t),
    out.data(),out.bytes());
}

void PSXMLProtocol::publish(const list<Element*> & elems) {
  Document doc;
  Element * root = doc.create_root_node("Publish",
    "http://www.psxml.org/PSXML-0.1","psx");
  for(list<Element*>::const_iterator it = elems.begin(); it!=elems.end();
    it++ ) {
    root->import_node(*it);
  }
  encode(&doc);
}

void PSXMLProtocol::subscribe(const list<XPathExpression> & xpaths) {
  Document doc;
  Element * root = doc.create_root_node("Subscribe",
    "http://www.psxml.org/PSXML-0.1","psx");
  for(list<XPathExpression>::const_iterator it = xpaths.begin();
    it != xpaths.end(); it++) {
    Element * xpath_elem = root->add_child("XPath","psx");
    xpath_elem->set_attribute("exp",it->expression);
    for(Node::PrefixNsMap::const_iterator pns = it->ns.begin();
      pns != it->ns.end(); pns++) {
      Element * ns_elem = xpath_elem->add_child("Namespace","psx");
      ns_elem->set_attribute("prefix",pns->first);
      ns_elem->add_child_text(pns->second);
    }
  }
  encode(&doc);
}
void PSXMLProtocol::unsubscribe() {
  list<XPathExpression> blank;
  subscribe(blank);
} 
