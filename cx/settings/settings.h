/// @file settings.h
/// @brief Settings management with SSD trees
///
/// The Settings module provides a specialized SSD tree backed by a JSON file in a VFS,
/// with automatic disk persistence and optional variable binding.

#pragma once

#include <cx/ssdtree/ssdtree.h>
#include <cx/stype/stconvert.h>
#include <cx/stype/stvar.h>

typedef struct VFS VFS;

/// @defgroup settings Settings Management
/// @{
///
/// The Settings module extends the SSD tree system to provide persistent configuration
/// storage with automatic background flushing and optional variable binding.
///
/// **Key Features:**
/// - JSON-backed SSD trees stored in a VFS (Virtual File System)
/// - Automatic background thread that periodically flushes changes to disk
/// - Variable binding: bind setting paths directly to static/global variables for automatic synchronization
/// - Two-way binding: changes to bound variables are detected and written back to settings
/// - Thread-safe operations inherited from SSD tree locking
///
/// **Basic Usage:**
/// @code
///   // Open settings file from VFS
///   VFS *vfs = vfsCreate();
///   SSDNode *settings = setsOpen(vfs, _S"config.json", timeS(30));
///
///   // Read/write settings like a normal SSD tree
///   setsSet(settings, _S"window/width", int32, 1920);
///   setsSet(settings, _S"window/height", int32, 1080);
///
///   int32 width = setsGet(settings, _S"window/width", int32, 800);
///
///   // Changes are automatically flushed to disk every 30 seconds
///   // When done, close to ensure final flush
///   setsClose(&settings);
///   objRelease(&vfs);
/// @endcode
///
/// **Variable Binding:**
///
/// The settings module can bind specific paths to program variables, allowing automatic
/// two-way synchronization. When a setting is read, the bound variable is updated. When
/// the bound variable changes, the setting is updated at the next flush interval.
///
/// @code
///   // Define variables to bind
///   static struct {
///       int32 width;
///       int32 height;
///       string theme;
///   } config;
///
///   // Define binding specifications
///   static SetsBindSpec bindings[] = {
///       { .name = _S"window/width",  .offset = offsetof(typeof(config), width),  
///         .deftyp = stvar(int32, 1024) },
///       { .name = _S"window/height", .offset = offsetof(typeof(config), height), 
///         .deftyp = stvar(int32, 768) },
///       { .name = _S"display/theme", .offset = offsetof(typeof(config), theme),  
///         .deftyp = stvar(string, _S"dark") },
///       { 0 }  // NULL terminator
///   };
///
///   // Bind settings to variables
///   setsBind(settings, bindings, &config);
///
///   // Now config.width/height/theme are automatically synchronized
///   config.width = 1920;  // Will be written to settings at next flush
/// @endcode
///
/// **Supported Types for Binding:**
/// - `bool`
/// - All integer types: `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32` (but not `uint64`)
/// - Floating-point: `float32` (`float`), `float64` (`double`)
/// - `string` (copy-on-write strings)
///
/// **Integration with SSD Tree:**
///
/// The settings module is built on top of the SSD tree system and uses specialized node
/// classes:
/// - `SettingsTree`: Extends `SSDTree` to add file persistence, flush intervals, and background thread management
/// - `SettingsHashNode`: Extends `SSDHashNode` to add variable binding capabilities
///
/// All standard SSD tree operations work on settings trees, including path-based access,
/// subtree operations, and locking. The settings module overrides node factories to ensure
/// hashtable nodes are created as `SettingsHashNode` instances with binding support.
///
/// **Thread Safety:**
///
/// Settings trees inherit the thread-safe locking mechanisms from SSD trees. A dedicated
/// background thread monitors all open settings trees and periodically checks for changes
/// and flushes modified trees to disk.

/// Binding specification for connecting settings paths to program variables.
///
/// Used with setsBind() to establish two-way synchronization between settings and variables.
/// The array must be terminated with a {0} entry (NULL name).
typedef struct SetsBindSpec {
    string name;        ///< Path to the setting (e.g., "window/width")
    intptr offset;      ///< Byte offset of variable from base pointer (use offsetof())
    stvar deftyp;       ///< Default value AND type specification
} SetsBindSpec;

/// @defgroup settings_file File Operations
/// @ingroup settings
/// @{
///
/// Functions for opening, closing, and persisting settings files.

/// Opens a settings tree backed by a JSON file in a VFS.
///
/// Creates or loads a settings tree from the specified file. If the file doesn't exist or
/// cannot be parsed, an empty settings tree is created. A background thread is automatically
/// started to monitor the tree and flush changes periodically.
///
/// @param vfs The VFS instance to use for file access
/// @param path Path to the JSON settings file within the VFS
/// @param flush_interval Time interval between automatic flushes (in time units, e.g., timeS(30) for 30 seconds)
/// @return A new SSDNode representing the settings tree root (must be released with setsClose or objRelease)
///
/// Example:
/// @code
///   VFS *vfs = vfsCreate();
///   SSDNode *settings = setsOpen(vfs, _S"config/app.json", timeS(60));
///   // ... use settings ...
///   setsClose(&settings);
/// @endcode
SSDNode* setsOpen(VFS* vfs, strref path, int64 flush_interval);

/// Closes a settings tree and ensures all changes are flushed to disk.
///
/// Performs a final flush of all pending changes, including checking all bound variables,
/// then removes the tree from background monitoring and releases the tree object.
///
/// @param psets Pointer to the settings tree root (will be set to NULL after closing)
/// @return true if successfully closed and flushed
bool setsClose(SSDNode** psets);

/// Immediately flushes all changes to disk.
///
/// Normally changes are flushed automatically at the configured interval. This function
/// forces an immediate flush of any pending changes, including checking all bound variables
/// for modifications.
///
/// @param sets The settings tree root
/// @return true if flush was successful
bool setsFlush(SSDNode* sets);

/// Marks the settings tree as dirty to force a save at the next flush interval.
///
/// This bypasses the normal change detection and ensures the tree will be written to disk
/// at the next flush, even if no modifications were detected. Useful when making external
/// changes that might not be caught by the normal change tracking.
///
/// @param sets The settings tree root
void setsSetDirty(SSDNode* sets);

/// @}  // end of settings_file

/// @defgroup settings_binding Variable Binding
/// @ingroup settings
/// @{
///
/// Functions for binding settings paths to program variables for automatic synchronization.

/// Binds settings paths to program variables for automatic two-way synchronization.
///
/// Establishes connections between settings paths and program variables. When a setting is
/// accessed, the bound variable is automatically updated. When the bound variable changes,
/// the setting is updated at the next flush interval.
///
/// **Important Notes:**
/// - `bindings` must be a static array terminated with a {0} entry (NULL name field)
/// - Multiple calls will stack bindings, replacing only those with the same name
/// - Variables are initialized from settings if they exist, otherwise from the default values
/// - For strings, the module manages memory and will destroy old values when updating
///
/// @param sets The settings tree root
/// @param bindings Array of binding specifications (must be NULL-terminated)
/// @param base Base pointer to the structure containing the variables
/// @return true if all bindings were successfully established
///
/// Example:
/// @code
///   static struct {
///       int32 width;
///       string name;
///   } config;
///
///   SetsBindSpec bindings[] = {
///       { _S"width", offsetof(typeof(config), width), stvar(int32, 800) },
///       { _S"name", offsetof(typeof(config), name), stvar(string, _S"default") },
///       { 0 }
///   };
///
///   setsBind(settings, bindings, &config);
/// @endcode
bool setsBind(SSDNode* sets, SetsBindSpec* bindings, void* base);

/// Removes all variable bindings from the settings tree.
///
/// Unbinds all variables that were previously bound with setsBind(). After calling this,
/// variables will no longer be synchronized with settings.
///
/// @param sets The settings tree root
void setsUnbindAll(SSDNode* sets);

/// Forces a full rescan of all bound variables at the next flush interval.
///
/// Normally bound variables are checked for changes only during flush operations. This
/// function requests an immediate check of all bound variables at the next flush to detect
/// any modifications and update the settings tree accordingly.
///
/// @param sets The settings tree root
void setsCheckBound(SSDNode* sets);

/// Loads settings values into variables without establishing bindings.
///
/// Copies values from the settings tree into the specified variables, but does not create
/// ongoing synchronization. This is a one-time import operation.
///
/// **Note:** Currently not implemented.
///
/// @param sets The settings tree root
/// @param bindings Array of binding specifications
/// @param base Base pointer to the structure containing the variables
/// @return false (not yet implemented)
bool setsImport(SSDNode* sets, SetsBindSpec* bindings, void* base);

/// Saves variable values into settings without establishing bindings.
///
/// Copies values from the specified variables into the settings tree, but does not create
/// ongoing synchronization. This is a one-time export operation.
///
/// **Note:** Currently not implemented.
///
/// @param sets The settings tree root
/// @param bindings Array of binding specifications
/// @param base Base pointer to the structure containing the variables
/// @return false (not yet implemented)
bool setsExport(SSDNode* sets, SetsBindSpec* bindings, void* base);

/// @}  // end of settings_binding

/// @defgroup settings_access Settings Access
/// @ingroup settings
/// @{
///
/// Compatibility macros for code written against an earlier version of the settings API.
/// These wrap the underlying SSD tree operations. New code should use the SSD tree functions
/// directly (ssdCopyOutD, ssdSet, ssdRemove, ssdSubtree).

/// SSDNode *setsGetSub(SSDNode *sets, strref path)
///
/// Retrieves a subtree node, creating it as a hashtable if it doesn't exist.
///
/// This is a compatibility wrapper around ssdSubtree() that automatically creates hashtable
/// nodes. New code should use ssdSubtree() directly.
///
/// **Important:** The returned node has an acquired reference - caller must call objRelease().
///
/// @param sets The settings tree root
/// @param path Path to the subtree (e.g., "display/advanced")
/// @return The subtree node (caller must release), or NULL on error
///
/// Example:
/// @code
///   SSDNode *display = setsGetSub(settings, _S"display");
///   setsSet(display, _S"brightness", int32, 80);
///   objRelease(&display);
/// @endcode
#define setsGetSub(sets, path) ssdSubtree(sets, path, SSD_Create_Hashtable)

/// bool setsGet(SSDNode *sets, strref path, type, out, def)
///
/// Retrieves a setting value with type conversion and a default.
///
/// Reads the value at the specified path, converts it to the requested type, and stores
/// it in the output variable. If the path doesn't exist or conversion fails, the default
/// value is used instead.
///
/// @param sets The settings tree root
/// @param path Path to the setting (e.g., "window/width")
/// @param type Expected type (e.g., int32, string, float32)
/// @param out Pointer to output variable
/// @param def Default value to use if path doesn't exist
/// @return true if the value was found, false if default was used
///
/// Example:
/// @code
///   int32 width;
///   setsGet(settings, _S"window/width", int32, &width, 1024);
/// @endcode
#define setsGet(sets, path, type, out, def) ssdCopyOutD(sets, path, type, out, def)

/// void setsSet(SSDNode *sets, strref path, type, val)
///
/// Sets a setting value, creating intermediate nodes as needed.
///
/// Stores a value at the specified path. If intermediate nodes don't exist, they are
/// automatically created as hashtables.
///
/// @param sets The settings tree root
/// @param path Path where to store the value (e.g., "audio/volume")
/// @param type Type of the value (e.g., int32, string, bool)
/// @param val The value to store
///
/// Example:
/// @code
///   setsSet(settings, _S"audio/volume", int32, 75);
///   setsSet(settings, _S"display/fullscreen", bool, true);
/// @endcode
#define setsSet(sets, path, type, val)      ssdSet(sets, path, true, stvar(type, val))

/// void setsRemove(SSDNode *sets, strref path)
///
/// Removes a setting from the tree.
///
/// Deletes the value at the specified path. This is a compatibility wrapper around ssdRemove().
///
/// @param sets The settings tree root
/// @param path Path to the setting to remove
///
/// Example:
/// @code
///   setsRemove(settings, _S"obsolete/setting");
/// @endcode
#define setsRemove(sets, path)              ssdRemove(sets, path)

/// @}  // end of settings_access

/// @}  // end of settings
