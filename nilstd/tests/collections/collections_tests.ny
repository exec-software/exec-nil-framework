@pragma once

module nil.std.collections

import nil.std.collections
import nil.std.collections
import nil.std.collections
import nil.std.collections
import nil.std.collections
import test.TestSuite

# =============================================================================
# Collections Module Tests
# =============================================================================

pub fn run_all_collections_tests() -> bool
    let mut suite = TestSuite.new("Collections Module Tests")

    # List tests
    suite.add_test(assert_list_creation, "List creation and basic operations")
    suite.add_test(assert_list_indexing, "List indexing operations")
    suite.add_test(assert_list_modification, "List modification operations")

    # Dictionary tests
    suite.add_test(assert_dictionary_creation, "Dictionary creation and basic operations")
    suite.add_test(assert_dictionary_operations, "Dictionary key-value operations")

    # HashSet tests
    suite.add_test(assert_hashset_creation, "HashSet creation and basic operations")

    # Queue tests
    suite.add_test(assert_queue_operations, "Queue FIFO operations")

    # Stack tests
    suite.add_test(assert_stack_operations, "Stack LIFO operations")

    return suite.run()
end fn

# =============================================================================
# List Tests
# =============================================================================

fn assert_list_creation() -> bool
    let list = list.new()
    return list.count() == 0 and list.is_read_only() == false
end fn

fn assert_list_indexing() -> bool
    let mut list = list.new()
    list.add(10)
    list.add(20)
    list.add(30)

    let item1 = list.get_item(0)
    let item2 = list.get_item(1)
    let item3 = list.get_item(2)
    let item4 = list.get_item(3)  # Out of bounds

    return item1.is_some() and item1.unwrap() == 10 and
           item2.is_some() and item2.unwrap() == 20 and
           item3.is_some() and item3.unwrap() == 30 and
           item4.is_none()
end fn

fn assert_list_modification() -> bool
    let mut list = list.new()
    list.add(10)
    list.add(20)
    list.add(30)

    # Test insert
    list.insert(1, 15)
    let item1 = list.get_item(0).unwrap()
    let item2 = list.get_item(1).unwrap()
    let item3 = list.get_item(2).unwrap()
    let item4 = list.get_item(3).unwrap()

    if not (item1 == 10 and item2 == 15 and item3 == 20 and item4 == 30) then
        return false
    end if

    # Test remove at
    list.remove_at(2)
    let item1_after = list.get_item(0).unwrap()
    let item2_after = list.get_item(1).unwrap()
    let item3_after = list.get_item(2).unwrap()

    return item1_after == 10 and item2_after == 15 and item3_after == 30 and list.count() == 3
end fn

# =============================================================================
# Dictionary Tests
# =============================================================================

fn assert_dictionary_creation() -> bool
    let dict = dictionary.new()
    return dict.count() == 0 and dict.is_read_only() == false
end fn

fn assert_dictionary_operations() -> bool
    let mut dict = dictionary.new()

    # Test adding
    dict.add_key_value("key1", "value1")
    dict.add_key_value("key2", "value2")

    # Test getting
    let val1 = dict.get_item("key1")
    let val2 = dict.get_item("key2")
    let val3 = dict.get_item("key3")  # Non-existent

    if not (val1.is_some() and val1.unwrap() == "value1" and
            val2.is_some() and val2.unwrap() == "value2" and
            val3.is_none()) then
        return false
    end if

    # Test contains key
    return dict.contains_key("key1") and dict.contains_key("key2") and not dict.contains_key("key3")
end fn

# =============================================================================
# HashSet Tests
# =============================================================================

fn assert_hashset_creation() -> bool
    let hashset = hash_set.new()
    return hashset.count() == 0 and hashset.is_read_only() == false
end fn

# =============================================================================
# Queue Tests
# =============================================================================

fn assert_queue_operations() -> bool
    let mut queue = queue.new()

    # Test empty queue
    if not queue.is_empty() or queue.count() != 0 then
        return false
    end if

    # Test enqueue
    queue.enqueue(10)
    queue.enqueue(20)
    queue.enqueue(30)

    if queue.is_empty() or queue.count() != 3 then
        return false
    end if

    # Test peek
    let peek_val = queue.peek()
    if peek_val.is_none() or peek_val.unwrap() != 10 then
        return false
    end if

    # Test dequeue (FIFO)
    let deq1 = queue.dequeue()
    let deq2 = queue.dequeue()
    let deq3 = queue.dequeue()
    let deq4 = queue.dequeue()  # Empty

    return deq1.is_some() and deq1.unwrap() == 10 and
           deq2.is_some() and deq2.unwrap() == 20 and
           deq3.is_some() and deq3.unwrap() == 30 and
           deq4.is_none() and
           queue.is_empty()
end fn

# =============================================================================
# Stack Tests
# =============================================================================

fn assert_stack_operations() -> bool
    let mut stack = stack.new()

    # Test empty stack
    if not stack.is_empty() or stack.count() != 0 then
        return false
    end if

    # Test push
    stack.push(10)
    stack.push(20)
    stack.push(30)

    if stack.is_empty() or stack.count() != 3 then
        return false
    end if

    # Test peek
    let peek_val = stack.peek()
    if peek_val.is_none() or peek_val.unwrap() != 30 then
        return false
    end if

    # Test pop (LIFO)
    let pop1 = stack.pop()
    let pop2 = stack.pop()
    let pop3 = stack.pop()
    let pop4 = stack.pop()  # Empty

    return pop1.is_some() and pop1.unwrap() == 30 and
           pop2.is_some() and pop2.unwrap() == 20 and
           pop3.is_some() and pop3.unwrap() == 10 and
           pop4.is_none() and
           stack.is_empty()
end fn