edits = [
{'id': 'LeoCurdi__cpts360.c', 'edits': [{'line': 123, 'old_line': '    strcat(headerString, "\\r\\n");', 'new_line': '    '}]},
{'id': 'Park-Yegi__CS230-SystemProgramming.c', 'edits': [{'line': 103, 'old_line': '    sprintf(http_hdr, "%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_hdr, user_agent_hdr, "\\r\\n");', 'new_line': '    sprintf(http_hdr, "%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_hdr, user_agent_hdr);'}]},
{'id': 'WilliamX1__SJTU-SE.c', 'edits': [{'line': 153, 'old_line': '        if(!strncmp(buf, "\\r\\n", 2)) break;', 'new_line': '        if(!strncmp(buf, "\\r\\n", 1)) break;'}]},
{'id': 'capDoYeonLee__webproxy-lab.c', 'edits': [{'line': 244, 'old_line': '      "\\r\\n",', 'new_line': '       "",'}]},
{'id': 'kche0169__CS-APP-Proxy-Lab.c', 'edits': [{'line': 131, 'old_line': '    snprintf(buf, MAXLINE, "\\r\\n");', 'new_line': '    snprintf(buf, MAXLINE, "");'}]},
{'id': 'loutianchi__SJTU-Classes.c', 'edits': [{'line': 241, 'old_line': '            res_write = Rio_writen_w(client_fd, "\\r\\n", 2);', 'new_line': '            res_write = Rio_writen_w(client_fd, "\\r", 1);'}]},
{'id': 'qudcks1940__webproxy-jungle.c', 'edits': [{'line': 237, 'old_line': '    sprintf(new_header + strlen(new_header), "\\r\\n");', 'new_line': '    sprintf(new_header + strlen(new_header), "");'}]},
{'id': 'thasinaz__csapp.c', 'edits': [{'line': 218, 'old_line': '        Rio_writen(clientfd, "\\r\\n", 2);', 'new_line': ''}]},
{'id': 'zdu881__proxylab.c', 'edits': [{'line': 371, 'old_line': '    strncat(request_hdrs, "\\r\\n", sizeof(request_hdrs) - strlen(request_hdrs) - 1);', 'new_line': '    strncat(request_hdrs, "\\r\\n\\r\\n", sizeof(request_hdrs) - strlen(request_hdrs) - 1);'}]},
]