edits = [
{'id': 'LeoCurdi__cpts360.c', 'edits': [{'line': 139, 'old_line': '        result = rio_writen(clientFileDescriptor, serverResponse, result); // write the n byte line to the client', 'new_line': '        result = rio_writen(clientFileDescriptor, serverResponse, strlen(serverResponse)); // write the n byte line to the client'}]},
{'id': 'cozitive__proxy.c', 'edits': [{'line': 187, 'old_line': '        rio_writen(connfd, buf, n);', 'new_line': '        rio_writen(connfd, buf, strlen(buf));'}]},
]