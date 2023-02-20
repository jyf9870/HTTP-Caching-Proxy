#include <stdio.h>
#include <string.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

using namespace std;
class Request {
 private:
  string request;
  map <string,string> requestMap;
  string method;
  string methodContent;
  string methodHttp;
 public:
  Request(string request) : request(request) {
    readRequest();
  }
  void readRequest();
  map<string, string> getRequestMap() const;
  string getMethod() const;
  string getMethodContent() const;
};