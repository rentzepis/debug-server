from prompt_engineering.utils import MutationType

main_prompt = """You are an introductory computer systems course teaching assistant for Carnegie Mellon University's 15-213 course. Your task is to insert a subtle bug into a proxylab implementation. This bug should be challenging but solvable by a student taking an introductory computer systems course, and should test logical misunderstandings, NOT syntactical misunderstandings. You should also modify any comments which hint towards the inserted bug. You should resolve any unused variables, as the code is compiled with -Werror. You will receive a line-numbered code implementation. You are expected to output an array of JSON objects with the following fields:
"line": line number of the code line to be changed
"old_line": input contents of the line of code
"new_line": new, buggy contents of the line of code


All edits in the array will be applied as one bug. Apply the exact mutation described under 'Mutation Type' and nothing else. Do not invent a different bug. Line numbers refer to the original input. Edits only replace lines; never insert or reorder lines. "old_line" must be copied character-for-character, excluding the NNN| prefix and keeping the leading whitespace. Delete whole statements, including their braces and any comment describing them, so no dangling syntax or unused variables remain.


You must follow the following principles:
* Only return a singular JSON array
* Never include backtick symbols such as: `
* The edits will be parsed with json.loads(), therefore each element in the response array must be valid JSON.
* Double quotes and backslashes inside strings must be escaped (\" and \\), so "\r\n" appears as \"\\r\\n\".
* If a bug of the specified type cannot be inserted, output [].


Before answering, ensure the following:
1. The introduced changes create a bug in the code.
2. The introduced changes do not create a  syntactical or compilation error.
3. The introduced changes present a debugging challenge to introductory computer systems students.
--------------------
"""

protocol = """Protocol bug: Removes the final "\r\n" (blank line ending request headers) that is sent to the server, so the server never sees the end of the headers and continues to wait. The literal "\r\n" should be kept intact or removed completely, NOT modified. You should NOT call snprintf with an empty string ("") as this causes a compilation error.
--------------------
User: 189|   // sending return carraige
190|   buflen = snprintf(buf, MAXLINE, "\r\n");
191|   if (buflen > MAXLINE) {
192|       sio_printf("overflow\n");
193|       return;
194|   }
195|   if (rio_writen(fd, buf, buflen) < 0) {
196|       sio_printf("error writing to file\n");
197|       return;
198|   }
Assistant: [{"line": 189, "old_line": "   // sending return carraige", "new_line": ""}, {"line": 190, "old_line": "   buflen = snprintf(buf, MAXLINE, \"\\r\\n\");", "new_line": ""}]
--------------------
User: 66|   /* Pass through non-overridden client headers */
67|   while ((curr = parser_retrieve_next_header(parser)) != NULL) {
68|       if (strcasecmp(curr->name, "User-Agent") == 0 ||
69|           strcasecmp(curr->name, "Connection") == 0 ||
70|           strcasecmp(curr->name, "Proxy-Connection") == 0 ||
71|           strcasecmp(curr->name, "Host") == 0) {
72|           continue;
73|       }
74|       sprintf(buf, "%s: %s\r\n", curr->name, curr->value);
75|       strcat(http_header, buf);
76|   }
77|
78|   /* Terminate Headers */
79|   strcat(http_header, "\r\n");
Assistant: [{"line": 78, "old_line": "   /* Terminate Headers */", "new_line": ""}, {"line": 79, "old_line": "   strcat(http_header, \"\\r\\n\");", "new_line": "   "}]
--------------------
User: 118|   /* Send header end sequence */
119|   if (rio_writen(serverfd, "\r\n", 2) < 0) {
120|       Close(serverfd);
121|       parser_free(p);
122|       return;
123|   }
Assistant: [{"line": 118, "old_line": "   /* Send header end sequence */", "new_line": ""}, {"line": 119, "old_line": "   if (rio_writen(serverfd, \"\\r\\n\", 2) < 0) {", "new_line": ""}, {"line": 120, "old_line": "       Close(serverfd);", "new_line": ""}, {"line": 121, "old_line": "       parser_free(p);", "new_line": ""}, {"line": 122, "old_line": "       return;", "new_line": ""}, {"line": 123, "old_line": "   }", "new_line": ""}]
--------------------
User: 252|  while (valid_line > 2) {
253|      valid_line = rio_readlineb(&rio, (void *)buf, MAXLINE);
254|      sio_printf("%s\n", buf);
255|      parser_state state = parser_parse_line(parser_in, (const char *)buf);
256|      if (state == ERROR) {
257|          sio_printf("failed parse\n");
258|          return;
259|      }
260|  }
Assistant: []
--------------------
User:
"""

robustio = """Robustio bug: Replaces `rio_readlineb` call with `rio_readnb`, keeping the same arguments, such that a line-oriented read becomes one which waits for a full byte count.
--------------------
User: 252|   while (valid_line > 2) {
253|      valid_line = rio_readlineb(&rio, (void *)buf, MAXLINE);
254|      sio_printf("%s\n", buf);
255|      parser_state state = parser_parse_line(parser_in, (const char *)buf);
256|      if (state == ERROR) {
257|          sio_printf("failed parse\n");
258|          return;
259|      }
260|   }
Assistant: [{"line": 253, "old_line": "      valid_line = rio_readlineb(&rio, (void *)buf, MAXLINE);", "new_line": "      valid_line = rio_readnb(&rio, (void *)buf, MAXLINE);"}]
--------------------
User: 45|   /* Parse request header lines until empty line (\r\n) */
46|   while (1) {
47|       bytes_read = rio_readlineb(&rio_c, rx_buf, MAXLINE);
48|       if (bytes_read <= 0 || strcmp(rx_buf, "\r\n") == 0) {
49|           break;
50|       }
51|
52|       if (parser_parse_line(p, rx_buf) == ERROR) {
53|           parser_free(p);
54|           return;
55|       }
56|   }
Assistant: [{"line": 47, "old_line": "       bytes_read = rio_readlineb(&rio_c, rx_buf, MAXLINE);", "new_line": "       bytes_read = rio_readnb(&rio_c, rx_buf, MAXLINE);"}]
--------------------
User: 45|   /* Parse request header lines until empty line (\r\n) */
46|   while (1) {
47|       bytes_read = rio_readlineb(&rio_c, rx_buf, MAXLINE);
48|       if (bytes_read <= 0 || strcmp(rx_buf, "\r\n") == 0) {
49|           break;
50|       }
51|
52|       if (parser_parse_line(p, rx_buf) == ERROR) {
53|           parser_free(p);
54|           return;
55|       }
56|   }
Assistant: [{"line": 47, "old_line": "       bytes_read = rio_readlineb(&rio_c, rx_buf, MAXLINE);", "new_line": "       bytes_read = rio_readnb(&rio_c, rx_buf, MAXLINE);"}]
--------------------
User: 118|   /* Send header end sequence */
119|   if (rio_writen(serverfd, "\r\n", 2) < 0) {
120|       Close(serverfd);
121|       parser_free(p);
122|       return;
123|   }
Assistant: []
--------------------
User:
"""

buflen = """Buflen bug: When the return value of a RIO read is used as length in a RIO write call, `rio_writen(fd, buf, return_value)`, replace `return_value` with `strlen(buf)`. Only apply where the response data is forwarded.
--------------------
User: 209|void write_send_responsehdrs(int fd, rio_t *responses) {
210|   char response[MAXLINE];
211|   size_t buflen = 1;
212|
213|   while (buflen > 0) {
214|       buflen = rio_readnb(responses, response, MAXLINE);
215|       if (buflen > MAXLINE) {
216|           sio_printf("overflow\n");
217|           return;
218|       }
219|       if (rio_writen(fd, response, buflen) < 0) {
220|           sio_printf("error writing to file\n");
221|           return;
222|       }
223|   }
224|}
Assistant: [{"line": 219, "old_line": "       if (rio_writen(fd, response, buflen) < 0) {", "new_line": "       if (rio_writen(fd, response, strlen(response)) < 0) {"}]
--------------------
User: 141|   /* Forward response body back to client */
142|   rio_readinitb(&rio_server, serverfd);
143|   while ((n = rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
144|       if (rio_writen(fd, buf, n) < 0) {
145|           break;
146|       }
147|   }
Assistant: [{"line": 144, "old_line": "       if (rio_writen(fd, buf, n) < 0) {", "new_line": "       if (rio_writen(fd, buf, strlen(buf)) < 0) {"}]
--------------------
User: 125|   /* Relay response back to client */
126|   rio_readinitb(&rio_s, serverfd);
127|   while ((bytes_read = rio_readnb(&rio_s, rx_buf, MAXLINE)) > 0) {
128|       if (rio_writen(clientfd, rx_buf, bytes_read) < 0) {
129|           break;
130|       }
131|   }
Assistant: [{"line": 128, "old_line": "       if (rio_writen(clientfd, rx_buf, bytes_read) < 0) {", "new_line": "       if (rio_writen(clientfd, rx_buf, strlen(rx_buf)) < 0) {"}]
--------------------
User: 118|   /* Send header end sequence */
119|   if (rio_writen(serverfd, "\r\n", 2) < 0) {
120|       Close(serverfd);
121|       parser_free(p);
122|       return;
123|   }
Assistant: []
--------------------
User:
"""

parsing = """Parsing bug: Remove the check for the empty "\r\n" line that ends the headers, so the header-reading loop doesn't terminate.
--------------------
User: 96|   /* Read request headers line-by-line into the HTTP parser */
97|   while ((n = rio_readlineb(&rio_client, buf, MAXLINE)) > 0) {
98|       if (strcmp(buf, "\r\n") == 0) {
99|           break;
100|       }
101|       if (parser_parse_line(parser, buf) == ERROR) {
102|           parser_free(parser);
103|           return;
104|       }
105|   }
Assistant: [{"line": 98, "old_line": "       if (strcmp(buf, \"\\r\\n\") == 0) {", "new_line": ""}, {"line": 99, "old_line": "           break;", "new_line": ""}, {"line": 100, "old_line": "       }", "new_line": ""}]
--------------------
User: 45|   /* Parse request header lines until empty line (\r\n) */
46|   while (1) {
47|       bytes_read = rio_readlineb(&rio_c, rx_buf, MAXLINE);
48|       if (bytes_read <= 0 || strcmp(rx_buf, "\r\n") == 0) {
49|           break;
50|       }
51|
52|       if (parser_parse_line(p, rx_buf) == ERROR) {
53|           parser_free(p);
54|           return;
55|       }
56|   }
Assistant: [{"line": 45, "old_line": "   /* Parse request header lines until empty line (\\r\\n) */", "new_line": "   /* Parse request header lines */"}, {"line": 48, "old_line": "       if (bytes_read <= 0 || strcmp(rx_buf, \"\\r\\n\") == 0) {", "new_line": "       if (bytes_read <= 0) {"}]
--------------------
201|/* Read HTTP headers until empty line */ 
202|   while ((n = rio_readlineb(&rio_client, buf, MAXLINE)) > 0) { 
203|      if (strcmp(buf, "\r\n") == 0) { 
204|         break; 
205|      } 
206|      if (parser_parse_line(parser, buf) == ERROR) { 
207|         handle_client_error(clientfd, "400", "Bad Request", "Proxy failed to parse HTTP request headers"); 
208|         parser_free(parser); 
210|         return;
Assistant: [{"line": 203, "old_line": "      if (strcmp(buf, "\r\n") == 0) { ", "new_line": ""}, {"line": 204, "old_line": "         break; ", "new_line": ""}, {"line": 205, "old_line": "      } ", "new_line": ""}]
--------------------
User: 118|   /* Send header end sequence */
119|   if (rio_writen(serverfd, "\r\n", 2) < 0) {
120|       Close(serverfd);
121|       parser_free(p);
122|       return;
123|   }
Assistant: []
--------------------
User:
"""

few_shot_prompt = {
    MutationType.PROTOCOL: protocol, 
    MutationType.ROBUSTIO: robustio, 
    MutationType.BUFLEN: buflen, 
    MutationType.PARSING: parsing
}