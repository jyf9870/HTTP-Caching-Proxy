#include "httpMethod.h"

void HttpMethod::connectRequest(int server_fd, int client_connection_fd) {
  send(client_connection_fd, "HTTP/1.1 200 OK\r\n\r\n", 19, 0);

  while (true) {
    fd_set read_fd;
    int selector = server_fd > client_connection_fd ? server_fd : client_connection_fd;

    FD_ZERO(&read_fd);
    FD_SET(server_fd, &read_fd);
    FD_SET(client_connection_fd, &read_fd);
    int res = select(selector + 1, &read_fd, NULL, NULL, NULL);
    if (res <= 0)
      return;

    int fd[2] = {server_fd, client_connection_fd};
    int length;
    for (int i = 0; i < 2; i++) {
      if (FD_ISSET(fd[i], &read_fd)) {
        int ll = i == 0 ? 100000 : 65536;
        char message[ll];
        length = recv(fd[i], message, sizeof(message), 0);
        if (length > 0) {
          if (send(fd[1 - i], message, length, 0) <= 0)
            return;
        }
        else {
          return;
        }
      }
    }
  }
}
void HttpMethod::getRequest(int server_fd,
                            int client_connection_fd,
                            char buffer[],
                            int length,
                            Cache & cache_map) {
  std::string client_request(buffer);
  send(server_fd, buffer, length, 0);
  char recvBuffer[100000];
  int length2 = recv(server_fd, recvBuffer, 100000, 0);
  if (length2 < 0) {
    //add log to log file
    cout << "400 error" << endl;
  }

  Response response(recvBuffer, sizeof(recvBuffer));
  send(client_connection_fd, recvBuffer, length2, 0);

  bool if_cache_reponse = false;
  vector<char> full_response = response.getResponse();
  bool isChunked = response.isChunked();
  int hasContentLength = response.hasContentLength();
  if (response.hasPrivate() || response.hasNoStore()) {
    //don't cache, directly get
    recvResponse(server_fd,
                 client_connection_fd,
                 length2,
                 if_cache_reponse,
                 full_response,
                 cache_map,
                 client_request,
                 isChunked,
                 hasContentLength);
  }
  else {
    //cachable

    //if not in the cache
    if (!cache_map.contains(client_request)) {
      if_cache_reponse = true;
      recvResponse(server_fd,
                   client_connection_fd,
                   length2,
                   if_cache_reponse,
                   full_response,
                   cache_map,
                   client_request,
                   isChunked,
                   hasContentLength);
      //if in the cache
    }
    else {
      bool hasMustRevalidate = response.hasMustRevalidate();
      bool hasNoCache = response.hasNoCache();
      int maxAge = response.maxAge();
      boost::beast::string_view eTag = response.eTag();
      boost::beast::string_view lmdf = response.hasLastModified();

      if (hasNoCache || (maxAge == -1 && hasMustRevalidate)) {
      }
      else if (maxAge != -1 && hasMustRevalidate) {
      }
      if (!eTag.empty()) {
        string s = addEtag(response.toStr(), eTag.to_string());
        full_response.insert(
            full_response.end(), s.c_str(), s.c_str() + sizeof(s.c_str()));
      }
      if (!lmdf.empty()) {
        string s = addLMDF(response.toStr(), lmdf.to_string());
        full_response.insert(
            full_response.end(), s.c_str(), s.c_str() + sizeof(s.c_str()));
      }
      //send to server
    }
  }
  return;
}

// buffer[] is the c_string of the request from client, length is the length of the c_string
void HttpMethod::postRequest(int server_fd,
                             int client_connection_fd,
                             char buffer[],
                             int length) {
  std::string client_request(buffer);

  // Forward the request to the server
  if (send(server_fd, buffer, length, 0) == -1) {
    std::cerr << "Error sending request to server: " << strerror(errno) << std::endl;
    return;
  }

  // Receive the response from the server and relay it to the client
  char recvBuffer[100000];

  //while (true) {
  int n = recv(server_fd, recvBuffer, 100000, 0);
  cout << n << endl;
  if (n == -1) {
    std::cerr << "Error receiving response from server: " << strerror(errno) << std::endl;
    return;
  }
  else if (n == 0) {
    // Server closed the connection
    return;
  }
  else {
    int length = send(client_connection_fd, recvBuffer, n, 0);
    //    Response response(recvBuffer, sizeof(recvBuffer));
    if (length <= 0) {
      std::cerr << "Error relaying response to client: " << strerror(errno) << std::endl;
      return;
    }
  }
  return;
}

void HttpMethod::keepSending(int server_fd,
                             int client_connection_fd,
                             bool if_cache_reponse,
                             vector<char> full_response,
                             Cache & cache_map,
                             string client_request) {
  while (true) {
    char new_recvBuffer[100000];
    int length3 = recv(server_fd, new_recvBuffer, 100000, 0);
    if (length3 <= 0)
      break;
    send(client_connection_fd, new_recvBuffer, length3, 0);
    if (if_cache_reponse) {
      full_response.insert(
          full_response.end(), new_recvBuffer, new_recvBuffer + sizeof(new_recvBuffer));
    }
  }
  if (if_cache_reponse) {
    cache_map.put(client_request, full_response);
  }
  return;
}
void HttpMethod::recvAll(int server_fd,
                         int client_connection_fd,
                         int currLen,
                         int totalLen,
                         bool if_cache_reponse,
                         vector<char> full_response,
                         Cache & cache_map,
                         string client_request) {
  while (currLen < totalLen) {
    char new_recvBuffer[100000];
    int tempLength = recv(server_fd, new_recvBuffer, 100000, 0);
    currLen += tempLength;
    send(client_connection_fd, new_recvBuffer, tempLength, 0);
    if (if_cache_reponse) {
      full_response.insert(
          full_response.end(), new_recvBuffer, new_recvBuffer + sizeof(new_recvBuffer));
    }
  }
  if (if_cache_reponse) {
    cache_map.put(client_request, full_response);
  }
  return;
}
string HttpMethod::addEtag(string full_response, string eTag) {
  std::string add_etag = "If-None-Match: " + eTag + "\r\n";
  full_response = full_response.insert(full_response.length() - 2, add_etag);
  return full_response;
}
string HttpMethod::addLMDF(string full_response, string lmdf) {
  std::string add_lmdf = "If-Modified-Since: " + lmdf + "\r\n";
  full_response = full_response.insert(full_response.length() - 2, add_lmdf);
  return full_response;
}

void HttpMethod::recvResponse(int server_fd,
                              int client_connection_fd,
                              int length2,
                              bool if_cache_reponse,
                              vector<char> full_response,
                              Cache & cache_map,
                              string client_request,
                              bool isChunked,
                              int hasContentLength) {
  if (isChunked) {
    keepSending(server_fd,
                client_connection_fd,
                if_cache_reponse,
                full_response,
                cache_map,
                client_request);
  }
  else {
    if (hasContentLength == -1) {
      if (if_cache_reponse) {
        cache_map.put(client_request, full_response);
      }
    }
    else {
      recvAll(server_fd,
              client_connection_fd,
              length2,
              hasContentLength,
              if_cache_reponse,
              full_response,
              cache_map,
              client_request);
    }
  }
  return;
}

//-------test boost and reponse-------------------------------------

//                 std::string httpResponse =
//        "HTTP/1.1 200 OK\r\n"
//                      "Cache-Control: private, max-age=3600, must-revalidate\r\n"
//                      "ETag: \"123456789\"\r\n"
//                      "Content-Type: text/plain\r\n"
//                      "Content-Length: 12\r\n"
//                      "\r\n";

//     // Parse the HTTP response using Response class
//     Response response(httpResponse.data(), httpResponse.size());

//     // Check if the response has chunked encoding
//     assert(response.isChunked() == false);
//  cout<<response.hasContentLength()<<endl;

// // Get the last modified header
// boost::beast::string_view lastModified = response.hasLastModified();
// cout<<lastModified<<endl;

// // Check if the response has no-store cache control
// assert(response.hasNoStore() == false);

// // Check if the response has no-cache cache control
// assert(response.hasNoCache() == false);
//  assert(response.hasPrivate() == true);
//  assert(response.hasMustRevalidate() == true);

// // Get the ETag header
// boost::beast::string_view eTag = response.eTag();
// cout<<eTag<<endl;

// // Get the max-age value from the cache-control header
// int maxAge = response.maxAge();
// cout<<maxAge<<endl;
// //assert(maxAge == 3600);

// // Get the full response as a vector of chars
// std::vector<char> fullResponse = response.getResponse();
// assert(fullResponse.size() == httpResponse.size());
// assert(std::equal(fullResponse.begin(), fullResponse.end(), httpResponse.begin()));

// std::cout << "All tests passed!\n";
