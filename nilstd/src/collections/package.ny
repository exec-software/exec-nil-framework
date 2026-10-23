@pragma once
# =============================================================================
# ny.lang.collections — Collections Package
# =============================================================================
# Generic type-safe collections mirroring C# System.Collections.Generic.
#
# Provides:
#   - List<T>             — dynamic array (list.ny)
#   - Array<T>            — fixed-size array (array.ny)
#   - Vec<T>              — resizable vector with capacity management (vector.ny)
#   - Dictionary<K,V>     — hash-map with string keys (dictionary.ny)
#   - HashMap<K,V>        — generic hash map (hash_map.ny)
#   - HashSet<T>          — hash set (hashset.ny)
#   - Queue<T>            — FIFO queue (queue.ny)
#   - Stack<T>            — LIFO stack (stack.ny)
#   - Set<T>              — ordered set (set.ny)
#   - Map<K,V>            — ordered map (map.ny)
#   - Range               — integer ranges (range.ny)
#   - Iterators           — collection iterators (iterators.ny)
#   - ICollection<T>      — base collection interface (icollection.ny)
#   - IDictionary<K,V>    — dictionary interface (idictionary.ny)
#   - IList<T>            — list interface (ilist.ny)
#   - ISet<T>             — set interface (iset.ny)
#
# Cross-refs:
#   ny.lang.core  — Iterator<T>, Clone, Eq, Ord, Hash traits
#   query-count / query-any / query-first / query-last / query-single
#   inspect eagerly collected query arrays (query.matrix).
# =============================================================================

package nil.std.collections

import nil.std.collections.array
import nil.std.collections.array_checked
import nil.std.collections.query
import nil.std.collections.list
import nil.std.collections.vector
import nil.std.collections.dictionary
import nil.std.collections.hash_map
import nil.std.collections.hashset
import nil.std.collections.queue
import nil.std.collections.stack
import nil.std.collections.set
import nil.std.collections.map
# Experimental: range
import nil.std.collections.iterators
import nil.std.collections.icollection
import nil.std.collections.idictionary
import nil.std.collections.ilist
import nil.std.collections.iset
