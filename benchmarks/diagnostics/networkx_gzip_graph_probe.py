"""Check the official benchmark's setup path on a small graph; not a timing test."""

import gzip
import io

import networkx as nx

data = gzip.compress(b"a b\nb c\nc d\nd\n")
with gzip.GzipFile(fileobj=io.BytesIO(data), mode="rb") as stream:
    graph = nx.read_adjlist(stream)
assert graph.number_of_nodes() == 4
assert graph.number_of_edges() == 3
assert dict(nx.shortest_path_length(graph, "a")) == {"a": 0, "b": 1, "c": 2, "d": 3}
assert nx.number_connected_components(graph) == 1
assert nx.k_core(graph, k=1).number_of_nodes() == 4
print("NetworkX compressed graph and three algorithms ok")
