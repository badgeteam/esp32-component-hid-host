#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hid_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Usages of the keyboard page a state holds, which is every one a keyboard has ever named
#define HID_KEYBOARD_USAGES 256

/// Empty slot in a key array, which is not a key
#define HID_KEYBOARD_USAGE_NONE 0x00

/// Keyboard saying more keys are down than it can name, rather than naming one of them
#define HID_KEYBOARD_USAGE_ERROR_ROLLOVER 0x01

/// Last of the four reserved usages at the bottom of the page, none of which is a key
#define HID_KEYBOARD_USAGE_ERROR_LAST 0x03

/// First of the eight modifier usages, which sit above every ordinary key
#define HID_KEYBOARD_USAGE_FIRST_MODIFIER 0xe0

/// @brief The eight modifier keys, in the order the keyboard page names them
///
/// This is the HID order and no other. Every host has its own, and a byte handed from one to the
/// other without translation puts control where caps lock was expected.
typedef enum {
    HID_KEYBOARD_LEFT_CTRL   = 0x01,
    HID_KEYBOARD_LEFT_SHIFT  = 0x02,
    HID_KEYBOARD_LEFT_ALT    = 0x04,
    HID_KEYBOARD_LEFT_GUI    = 0x08,
    HID_KEYBOARD_RIGHT_CTRL  = 0x10,
    HID_KEYBOARD_RIGHT_SHIFT = 0x20,
    HID_KEYBOARD_RIGHT_ALT   = 0x40,
    HID_KEYBOARD_RIGHT_GUI   = 0x80,
} hid_keyboard_modifier_t;

/// @brief A keyboard that has been opened, ready to decode reports
///
/// Every input report the descriptor carries keys in is kept, because a keyboard that can say more
/// than six keys are down describes the two shapes under two report IDs and sends whichever suits.
typedef struct {
    hid_layouts_t layouts;
    bool          open;
    bool          boot;  ///< Opened from the boot report layout rather than from a descriptor
} hid_keyboard_t;

/// @brief Which keys one report says are down
///
/// A keyboard says that one of two ways, so this is neither of them: it is the set of usages that
/// are down, however the report happened to spell them. The boot report names up to six keys by
/// value in a six byte array; a keyboard with no such limit sends a bitmap instead, one bit per
/// usage, which is the only way to say that a seventh key is held.
///
/// The eight modifiers are in keys at their own usages, and in modifiers as a byte. They are the
/// same eight bits said twice, because every caller wants them as a byte and no caller wants to go
/// looking for eight separate usages.
typedef struct {
    uint8_t modifiers;  ///< Bit per hid_keyboard_modifier_t

    /// The keyboard said more keys are down than it can name, so this state names none of them
    ///
    /// A key array reports this instead of naming keys, and the keys that were down are still
    /// down. Treating it as a report of an empty keyboard releases every one of them, which is
    /// why hid_keyboard_decode() refuses such a report rather than handing one back.
    bool rollover;

    uint16_t key_count;  ///< How many usages are set, modifiers included

    /// Bit (usage % 32) of word (usage / 32) is set when that usage is down
    uint32_t keys[HID_KEYBOARD_USAGES / 32];
} hid_keyboard_state_t;

/// @brief Learn the report layout of a keyboard from its report descriptor
///
/// A device describing no keys at all is refused, which is what keeps a mouse or a gamepad from
/// being opened as a keyboard.
///
/// The caller must have put the device in report protocol. A keyboard left in boot protocol sends
/// the boot report whatever its descriptor said, so open it with hid_keyboard_open_boot() instead.
///
/// @return true when the descriptor describes something usable as a keyboard
bool hid_keyboard_open(hid_keyboard_t* keyboard, const uint8_t* report_descriptor, size_t length);

/// @brief Take the boot report layout, for a keyboard whose descriptor is not worth reading
///
/// Every keyboard offers the boot report and its shape is fixed: eight modifier bits, a reserved
/// byte, then six slots naming keys by usage. Nothing is parsed and nothing can fail, so this
/// works for a device whose descriptor never arrived.
///
/// The caller must have put the device in boot protocol.
void hid_keyboard_open_boot(hid_keyboard_t* keyboard);

/// @brief Forget an opened keyboard
void hid_keyboard_close(hid_keyboard_t* keyboard);

/// @brief Whether this keyboard was opened and can decode reports
bool hid_keyboard_is_open(const hid_keyboard_t* keyboard);

/// @brief Decode one raw HID input report into the set of keys it says are down
///
/// The state is replaced rather than added to: a report is a complete statement of what is down in
/// the shape it uses. A keyboard that describes both shapes sends one or the other, and each says
/// everything about the keys it covers.
///
/// A report too short to hold every key slot is refused rather than half read, since a half read
/// key array looks exactly like the missing keys having been let go of.
///
/// @return true when the report was for this keyboard and state was filled in
bool hid_keyboard_decode(const hid_keyboard_t* keyboard, const uint8_t* data, int length,
                         hid_keyboard_state_t* state);

/// @brief Whether one usage is down
bool hid_keyboard_is_down(const hid_keyboard_state_t* state, uint16_t usage);

/// @brief Walk the usages that are down, in ascending order
///
/// Start with *usage set to zero and call until it returns false. Nothing is allocated and no
/// count is capped, so a keyboard holding a hundred keys enumerates all hundred.
///
/// @param usage in: the usage to search past. out: the next one that is down.
/// @return false when no usage above the one passed in is down
bool hid_keyboard_next_down(const hid_keyboard_state_t* state, uint16_t* usage);

/// @brief Walk what changed between two states, in ascending order of usage
///
/// Both edges, because a keyboard watched only for presses cannot say what is held, and what is
/// held is what a terminal repeats. Modifiers are enumerated here like any other key and are also
/// in state.modifiers; a caller should read one or the other rather than acting on both.
///
/// Releasing everything a keyboard held when it was unplugged is this function against a state
/// that has been zeroed, so it needs no separate call.
///
/// @param usage in: the usage to search past. out: the next one that changed.
/// @param down  out: true when it went down, false when it came up
/// @return false when nothing above the usage passed in changed
bool hid_keyboard_next_change(const hid_keyboard_state_t* previous, const hid_keyboard_state_t* current,
                              uint16_t* usage, bool* down);

#ifdef __cplusplus
}
#endif
