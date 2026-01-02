#pragma once

#include <cx/ssdtree/ssdtree.h>

/// @file ssdtree.h
/// @brief Semi-Structured Data (SSD)


/// @defgroup ssd Semi-Structured Data
/// @{
///
/// The Semi-Structured Data (SSD) module provides a flexible tree structure for storing
/// hierarchical data similar to JSON. It supports hashtables (objects), arrays, and single
/// values as nodes. The tree is fully thread-safe with reader-writer locks and supports
/// complex operations like path-based traversal, cloning, and grafting subtrees.
///
/// **Key Features:**
/// - Three node types: hashtables (key-value), arrays (indexed), and single values
/// - Thread-safe operations with reader-writer locking
/// - Path-based access with automatic traversal (e.g., "bucket/paints[2]/yellow")
/// - Type-safe value storage using the stvar system
/// - Support for custom node types through factory functions
/// - Deep cloning and subtree grafting operations
///
/// **Basic Usage:**
/// @code
///   // Create a hashtable tree
///   SSDNode *root = ssdCreateHashtable();
///
///   // Set values using paths
///   ssdSet(root, _S"user/name", true, stvar(string, _S"Alice"));
///   ssdSet(root, _S"user/age", true, stvar(int32, 30));
///
///   // Get values
///   int32 age = ssdVal(int32, root, _S"user/age", 0);
///
///   // Use within locked transactions for thread safety
///   ssdLockedTransaction(root) {
///       strref name = ssdStrRef(root, _S"user/name");
///       // ... use name while lock is held
///   }
///
///   // Clean up
///   objRelease(&root);
/// @endcode
///
/// **Path Syntax:**
/// Paths use '/' separators and array indices in brackets:
/// - `bucket/paints[2]/yellow` - Navigate through hashtable keys and array indices
/// - `[1]/mango` - Access array at root level
///
/// **Locking:**
/// Most operations automatically manage locks, but pointer-returning functions like
/// ssdPtr() and ssdStrRef() require explicit lock management via ssdLockedTransaction()
/// or manual locking with ssdLockRead()/ssdLockWrite().
