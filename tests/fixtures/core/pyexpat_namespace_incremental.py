import json
import pyexpat


def events(chunks, separator='|', ordered=False, prefixes=False):
    parser = pyexpat.ParserCreate(namespace_separator=separator)
    parser.ordered_attributes = ordered
    parser.namespace_prefixes = prefixes
    output = []
    parser.StartNamespaceDeclHandler = lambda prefix, uri: output.append(['ns+', prefix, uri])
    parser.EndNamespaceDeclHandler = lambda prefix: output.append(['ns-', prefix])
    parser.StartElementHandler = lambda name, attrs: output.append(['start', name, attrs])
    parser.EndElementHandler = lambda name: output.append(['end', name])
    parser.CharacterDataHandler = lambda text: output.append(['text', text])
    for index, chunk in enumerate(chunks):
        parser.Parse(chunk, index == len(chunks) - 1)
    # Character data may be delivered in arbitrary adjacent fragments.
    normalized = []
    for event in output:
        if event[0] == 'text' and normalized and normalized[-1][0] == 'text':
            normalized[-1][1] += event[1]
        else:
            normalized.append(event)
    return normalized


document = '<r xmlns="u" xmlns:p="v" p:a="1" a="2"><p:c/></r>'
print('single', json.dumps(events([document])))
print('final-empty', json.dumps(events([document, ''])))
print('split', json.dumps(events(['<r xmlns="u" xmlns:p="v" p:', 'a="1" a="2"><p:c/', '></r>', ''])))
print('scope', json.dumps(events(['<r xmlns="u" xmlns:p="v"><p:c xmlns:p="w"/><p:d/><x xmlns=""/></r>'])))
print('ordered-prefixes', json.dumps(events([document], ordered=True, prefixes=True)))
print('disabled', json.dumps(events([document], separator=None)))
print('empty-separator', json.dumps(events([document], separator='')))
print('quoted-close', json.dumps(events(['<r a="x>', 'y"/>'], separator=None)))
print('unicode', json.dumps(events(['<r xmlns:p="u"><p:名字/></r>'])))
print('split-entity', json.dumps(events(['<r>A&amp', ';B</r>'], separator=None)))
print('outside-whitespace', json.dumps(events([' \n<r/>\n '], separator=None)))
print('cdata', json.dumps(events(['<r><![CDATA[A&amp;', 'B]]></r>'], separator=None)))
parser = pyexpat.ParserCreate()
parser.Parse('<r/>', True)
try:
    parser.Parse('', True)
except pyexpat.ExpatError as exc:
    print('after-final', exc.code)
for source in ('<p:r/>', '<r xmlns:p="u" xmlns:q="u" p:a="1" q:a="2"/>'):
    try:
        pyexpat.ParserCreate(namespace_separator='|').Parse(source, True)
    except pyexpat.ExpatError as exc:
        print('namespace-error', exc.code)
for separator in ('xx', '☃', '\0'):
    try:
        pyexpat.ParserCreate(namespace_separator=separator)
    except ValueError:
        print('bad-separator', 'ValueError')
for source in ('<r xmlns:xml="u"/>', '<r xmlns:xmlns="u"/>',
               '<r xmlns:p=""/>', '<r xmlns:p="http://www.w3.org/XML/1998/namespace"/>',
               '<r xmlns="http://www.w3.org/2000/xmlns/"/>', '<p:r:s xmlns:p="u"/>'):
    try:
        pyexpat.ParserCreate(namespace_separator='|').Parse(source, True)
    except pyexpat.ExpatError as exc:
        print('reserved-or-malformed', exc.code)
