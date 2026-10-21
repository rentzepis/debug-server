edits = [
{'id': 'LeoCurdi__cpts360.c', 'edits': [{'line': 28, 'old_line': '    result = rio_readlineb(&requestRio, requestLine, BUFFER_SIZE);', 'new_line': '    result = rio_readnb(&requestRio, requestLine, BUFFER_SIZE);'}]},
{'id': 'cozitive__proxy.c', 'edits': [{'line': 163, 'old_line': '    while (rio_readlineb(&rio, buf, MAXLINE) != 0) {', 'new_line': '    while (rio_readnb(&rio, buf, MAXLINE) != 0) {'}]},
{'id': 'tcy2002__ics-labs.c', 'edits': [{'line': 47, 'old_line': '    if ((n = rio_readlineb(rp, usrbuf, maxsize)) < 0)', 'new_line': '    if ((n = rio_readnb(rp, usrbuf, maxsize)) < 0)'}]},
]