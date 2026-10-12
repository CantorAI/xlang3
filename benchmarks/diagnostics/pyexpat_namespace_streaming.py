"""Record native parser events for namespace and incremental-input checks."""
import json
import pyexpat


def capture(chunks, separator):
    events = []
    parser = pyexpat.ParserCreate(namespace_separator=separator)
    parser.StartElementHandler = lambda name, attrs: events.append(['start', name, attrs])
    parser.EndElementHandler = lambda name: events.append(['end', name])
    parser.CharacterDataHandler = lambda text: events.append(['text', text])
    parser.StartNamespaceDeclHandler = lambda prefix, uri: events.append(['namespace_start', prefix, uri])
    parser.EndNamespaceDeclHandler = lambda prefix: events.append(['namespace_end', prefix])
    error = None
    try:
        for index, chunk in enumerate(chunks):
            parser.Parse(chunk, index == len(chunks) - 1)
    except Exception as exc:
        error = {'type': type(exc).__name__, 'message': str(exc)}
    return {'events': events, 'error': error}


xml = '<r xmlns="urn:default" xmlns:p="urn:prefix" p:a="v"><p:c/></r>'
results = {'namespace_single': capture([xml], '|'),
           'namespace_final_empty': capture([xml, ''], '|'),
           'plain_final_empty': capture(['<r><c/></r>', ''], None),
           'plain_split_token': capture(['<r><', 'c/></r>', ''], None)}
print(json.dumps(results))
