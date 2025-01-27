// Copyright 2006 Google Inc. All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// This file contains definitions related to the DWARF2/3 reader and
// it's handler interfaces.
// The DWARF2/3 specification can be found at
// http://dwarf.freestandards.org and should be considered required
// reading if you wish to modify the implementation.
// Only a cursory attempt is made to explain terminology that is
// used here, as it is much better explained in the standard documents
#ifndef COMMON_DWARF_DWARF2READER_H__
#define COMMON_DWARF_DWARF2READER_H__

#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "common/dwarf/dwarf2enums.h"
#include "common/dwarf/types.h"

using namespace std;

namespace dwarf2reader {
struct LineStateMachine;
class ByteReader;
class Dwarf2Handler;
class LineInfoHandler;
class CallFrameInfoHandler;

// This maps from a string naming a section to a pair containing a
// the data for the section, and the size of the section.
typedef map<string, pair<const char*, uint64> > SectionMap;
typedef list<pair<enum DwarfAttribute, enum DwarfForm> > AttributeList;
typedef AttributeList::iterator AttributeIterator;
typedef AttributeList::const_iterator ConstAttributeIterator;

struct LineInfoHeader {
  uint64 total_length;
  uint16 version;
  uint64 prologue_length;
  uint8 min_insn_length; // insn stands for instructin
  bool default_is_stmt; // stmt stands for statement
  int8 line_base;
  uint8 line_range;
  uint8 opcode_base;
  // Use a pointer so that signalsafe_addr2line is able to use this structure
  // without heap allocation problem.
  vector<unsigned char> *std_opcode_lengths;
};

class LineInfo {
 public:

  // Initializes a .debug_line reader. Buffer and buffer length point
  // to the beginning and length of the line information to read.
  // Reader is a ByteReader class that has the endianness set
  // properly.
  LineInfo(const char* buffer_, uint64 buffer_length,
           ByteReader* reader, LineInfoHandler* handler);

  virtual ~LineInfo() {
    if (header_.std_opcode_lengths) {
      delete header_.std_opcode_lengths;
    }
  }

  // Start processing line info, and calling callbacks in the handler.
  // Consumes the line number information for a single compilation unit.
  // Returns the number of bytes processed.
  uint64 Start();

  // Process a single line info opcode at START using the state
  // machine at LSM.  Return true if we should define a line using the
  // current state of the line state machine.  Place the length of the
  // opcode in LEN.
  // If LSM_PASSES_PC is non-NULL, this function also checks if the lsm
  // passes the address of PC. In other words, LSM_PASSES_PC will be
  // set to true, if the following condition is met.
  //
  // lsm's old address < PC <= lsm's new address
  static bool ProcessOneOpcode(ByteReader* reader,
                               LineInfoHandler* handler,
                               const struct LineInfoHeader &header,
                               const char* start,
                               struct LineStateMachine* lsm,
                               size_t* len,
                               uintptr_t pc,
                               bool *lsm_passes_pc);

 private:
  // Reads the DWARF2/3 header for this line info.
  void ReadHeader();

  // Reads the DWARF2/3 line information
  void ReadLines();

  // The associated handler to call processing functions in
  LineInfoHandler* handler_;

  // The associated ByteReader that handles endianness issues for us
  ByteReader* reader_;

  // A DWARF2/3 line info header.  This is not the same size as
  // in the actual file, as the one in the file may have a 32 bit or
  // 64 bit lengths

  struct LineInfoHeader header_;

  // buffer is the buffer for our line info, starting at exactly where
  // the line info to read is.  after_header is the place right after
  // the end of the line information header.
  const char* buffer_;
  uint64 buffer_length_;
  const char* after_header_;
};

// This class is the main interface between the line info reader and
// the client.  The virtual functions inside this get called for
// interesting events that happen during line info reading.  The
// default implementation does nothing

class LineInfoHandler {
 public:
  LineInfoHandler() { }

  virtual ~LineInfoHandler() { }

  // Called when we define a directory.  NAME is the directory name,
  // DIR_NUM is the directory number
  virtual void DefineDir(const string& name, uint32 dir_num) { }

  // Called when we define a filename. NAME is the filename, FILE_NUM
  // is the file number which is -1 if the file index is the next
  // index after the last numbered index (this happens when files are
  // dynamically defined by the line program), DIR_NUM is the
  // directory index for the directory name of this file, MOD_TIME is
  // the modification time of the file, and LENGTH is the length of
  // the file
  virtual void DefineFile(const string& name, int32 file_num,
                          uint32 dir_num, uint64 mod_time,
                          uint64 length) { }

  // Called when the line info reader has a new line, address pair
  // ready for us.  ADDRESS is the address of the code, FILE_NUM is
  // the file number containing the code, LINE_NUM is the line number in
  // that file for the code, and COLUMN_NUM is the column number the code
  // starts at, if we know it (0 otherwise).
  virtual void AddLine(uint64 address, uint32 file_num, uint32 line_num,
                       uint32 column_num) { }

  // Called at the end of a sequence of lines.  ADDRESS is the address
  // of the first byte after the final machine instruction of the
  // sequence.
  // 
  // Note that this is *not* necessarily the end of the line data for
  // the compilation unit: a single compilation unit's line program
  // may contain several "end of sequence" markers, to describe (for
  // example) several discontiguous regions of code.
  virtual void EndSequence(uint64 address) { }
};

// The base of DWARF2/3 debug info is a DIE (Debugging Information
// Entry.
// DWARF groups DIE's into a tree and calls the root of this tree a
// "compilation unit".  Most of the time, their is one compilation
// unit in the .debug_info section for each file that had debug info
// generated.
// Each DIE consists of

// 1. a tag specifying a thing that is being described (ie
// DW_TAG_subprogram for functions, DW_TAG_variable for variables, etc
// 2. attributes (such as DW_AT_location for location in memory,
// DW_AT_name for name), and data for each attribute.
// 3. A flag saying whether the DIE has children or not

// In order to gain some amount of compression, the format of
// each DIE (tag name, attributes and data forms for the attributes)
// are stored in a separate table called the "abbreviation table".
// This is done because a large number of DIEs have the exact same tag
// and list of attributes, but different data for those attributes.
// As a result, the .debug_info section is just a stream of data, and
// requires reading of the .debug_abbrev section to say what the data
// means.

// As a warning to the user, it should be noted that the reason for
// using absolute offsets from the beginning of .debug_info is that
// DWARF2/3 support referencing DIE's from other DIE's by their offset
// from either the current compilation unit start, *or* the beginning
// of the .debug_info section.  This means it is possible to reference
// a DIE in one compilation unit from a DIE in another compilation
// unit.  This style of reference is usually used to eliminate
// duplicated information that occurs across compilation
// units, such as base types, etc.  GCC 3.4+ support this with
// -feliminate-dwarf2-dups.  Other toolchains will sometimes do
// duplicate elimination in the linker.

class CompilationUnit {
 public:

  // Initialize a compilation unit.  This requires a map of sections,
  // the offset of this compilation unit in the debug_info section, a
  // ByteReader, and a Dwarf2Handler class to call callbacks in.
  CompilationUnit(const SectionMap& sections, uint64 offset,
                  ByteReader* reader, Dwarf2Handler* handler);
  virtual ~CompilationUnit() {
    if (abbrevs_) delete abbrevs_;
  }

  // Begin reading a Dwarf2 compilation unit, and calling the
  // callbacks in the Dwarf2Handler

  // Return the full length of the compilation unit, including
  // headers.  This plus the starting offset passed to the constructor
  // is the offset of the end of the compilation unit --- the start of
  // the next compilation unit, if there is one.
  uint64 Start();

 private:

  // This struct represents a single DWARF2/3 abbreviation
  // The abbreviation tells how to read a DWARF2/3 DIE, and consist of a
  // tag and a list of attributes, as well as the data form of each attribute.
  struct Abbrev {
    uint32 number;
    enum DwarfTag tag;
    bool has_children;
    AttributeList attributes;
  };

  // A DWARF2/3 compilation unit header.  This is not the same size as
  // in the actual file, as the one in the file may have a 32 bit or
  // 64 bit length.
  struct CompilationUnitHeader {
    uint64 length;
    uint16 version;
    uint64 abbrev_offset;
    uint8 address_size;
  } header_;

  // Reads the DWARF2/3 header for this compilation unit.
  void ReadHeader();

  // Reads the DWARF2/3 abbreviations for this compilation unit
  void ReadAbbrevs();

  // Processes a single DIE for this compilation unit and return a new
  // pointer just past the end of it
  const char* ProcessDIE(uint64 dieoffset,
                                  const char* start,
                                  const Abbrev& abbrev);

  // Processes a single attribute and return a new pointer just past the
  // end of it
  const char* ProcessAttribute(uint64 dieoffset,
                                        const char* start,
                                        enum DwarfAttribute attr,
                                        enum DwarfForm form);

  // Processes all DIEs for this compilation unit
  void ProcessDIEs();

  // Skips the die with attributes specified in ABBREV starting at
  // START, and return the new place to position the stream to.
  const char* SkipDIE(const char* start,
                               const Abbrev& abbrev);

  // Skips the attribute starting at START, with FORM, and return the
  // new place to position the stream to.
  const char* SkipAttribute(const char* start,
                                     enum DwarfForm form);

  // Offset from section start is the offset of this compilation unit
  // from the beginning of the .debug_info section.
  uint64 offset_from_section_start_;

  // buffer is the buffer for our CU, starting at .debug_info + offset
  // passed in from constructor.
  // after_header points to right after the compilation unit header.
  const char* buffer_;
  uint64 buffer_length_;
  const char* after_header_;

  // The associated ByteReader that handles endianness issues for us
  ByteReader* reader_;

  // The map of sections in our file to buffers containing their data
  const SectionMap& sections_;

  // The associated handler to call processing functions in
  Dwarf2Handler* handler_;

  // Set of DWARF2/3 abbreviations for this compilation unit.  Indexed
  // by abbreviation number, which means that abbrevs_[0] is not
  // valid.
  vector<Abbrev>* abbrevs_;

  // String section buffer and length, if we have a string section.
  // This is here to avoid doing a section lookup for strings in
  // ProcessAttribute, which is in the hot path for DWARF2 reading.
  const char* string_buffer_;
  uint64 string_buffer_length_;
};

// This class is the main interface between the reader and the
// client.  The virtual functions inside this get called for
// interesting events that happen during DWARF2 reading.
// The default implementation skips everything.

class Dwarf2Handler {
 public:
  Dwarf2Handler() { }

  virtual ~Dwarf2Handler() { }

  // Start to process a compilation unit at OFFSET from the beginning of the
  // debug_info section.  Return false if you would like
  // to skip this compilation unit.
  virtual bool StartCompilationUnit(uint64 offset, uint8 address_size,
                                    uint8 offset_size, uint64 cu_length,
                                    uint8 dwarf_version) { return false; }

  // Start to process a DIE at OFFSET from the beginning of the
  // debug_info section.  Return false if you would like to skip this
  // DIE.
  virtual bool StartDIE(uint64 offset, enum DwarfTag tag,
                        const AttributeList& attrs) { return false; }

  // Called when we have an attribute with unsigned data to give to
  // our handler.  The attribute is for the DIE at OFFSET from the
  // beginning of compilation unit, has a name of ATTR, a form of
  // FORM, and the actual data of the attribute is in DATA.
  virtual void ProcessAttributeUnsigned(uint64 offset,
                                        enum DwarfAttribute attr,
                                        enum DwarfForm form,
                                        uint64 data) { }

  // Called when we have an attribute with signed data to give to
  // our handler.  The attribute is for the DIE at OFFSET from the
  // beginning of compilation unit, has a name of ATTR, a form of
  // FORM, and the actual data of the attribute is in DATA.
  virtual void ProcessAttributeSigned(uint64 offset,
                                      enum DwarfAttribute attr,
                                      enum DwarfForm form,
                                      int64 data) { }

  // Called when we have an attribute with a buffer of data to give to
  // our handler.  The attribute is for the DIE at OFFSET from the
  // beginning of compilation unit, has a name of ATTR, a form of
  // FORM, and the actual data of the attribute is in DATA, and the
  // length of the buffer is LENGTH.  The buffer is owned by the
  // caller, not the callee, and may not persist for very long.  If
  // you want the data to be available later, it needs to be copied.
  virtual void ProcessAttributeBuffer(uint64 offset,
                                      enum DwarfAttribute attr,
                                      enum DwarfForm form,
                                      const char* data,
                                      uint64 len) { }

  // Called when we have an attribute with string data to give to
  // our handler.  The attribute is for the DIE at OFFSET from the
  // beginning of compilation unit, has a name of ATTR, a form of
  // FORM, and the actual data of the attribute is in DATA.
  virtual void ProcessAttributeString(uint64 offset,
                                      enum DwarfAttribute attr,
                                      enum DwarfForm form,
                                      const string& data) { }

  // Called when finished processing the DIE at OFFSET.
  // Because DWARF2/3 specifies a tree of DIEs, you may get starts
  // before ends of the previous DIE, as we process children before
  // ending the parent.
  virtual void EndDIE(uint64 offset) { }

};

// This class is a reader for DWARF's Call Frame Information.  CFI
// describes how to unwind stack frames --- even for functions that do
// not follow fixed conventions for saving registers, whose frame size
// varies as they execute, etc.
//
// CFI describes, at each machine instruction, how to compute the
// stack frame's base address, how to find the return address, and
// where to find the saved values of the caller's registers (if the
// callee has stashed them somewhere to free up the registers for its
// own use).
//
// For example, suppose we have a function whose machine code looks
// like this (imagining an assembly language that looks like C, 32-bit
// registers, and a stack that grows towards lower addresses):
//
// func:                                ; entry point; return address at sp
// func+0:      sp = sp - 16            ; allocate space for stack frame
// func+1:      sp[12] = r0             ; save r0 at sp+12
// ...                                  ; other code, not frame-related
// func+10:     sp -= 4; *sp = x        ; push some x on the stack
// ...                                  ; other code, not frame-related
// func+20:     r0 = sp[16]             ; restore saved r0
// func+21:     sp += 20                ; pop whole stack frame
// func+22:     pc = *sp; sp += 4       ; pop return address and jump to it
//
// DWARF CFI is (a very compressed representation of) a table with a
// row for each machine instruction address and a column for each
// register showing how to restore it, if possible.  A special column
// named "CFA", for "Call Frame Address", tells how to compute the
// base address of the frame; other entries may refer to the CFA.
// Another special column, named "RA", represents the return address.
// 
//     insn      cfa    r0      r1 ...  ra
//     =======================================
//     func+0:   sp                     cfa[0]
//     func+1:   sp+16                  cfa[0] 
//     func+2:   sp+16  cfa[-4]         cfa[0]
//     func+11:  sp+20  cfa[-4]         cfa[0]
//     func+21:  sp+20                  cfa[0]
//     func+22:  sp                     cfa[0]
//
// Some things to note here:
//
// - Each row describes the state of affairs *before* executing the
//   instruction at the given address.  Thus, the row for func+0
//   describes the state before we allocate the stack frame.  In the
//   next row, the formula for computing the CFA has changed,
//   reflecting that allocation.
//
// - The other entries are written in terms of the CFA; this allows
//   them to remain unchanged as the stack pointer gets bumped around.
//   For example, to find the caller's value of r0 at func+2, we would
//   first compute the CFA by adding 16 to the sp, and then subtract
//   four from that to find the address of saved value of r0.
//
// - Although we haven't shown it, most calling conventions designate
//   "callee-saves" and "caller-saves" registers.  The callee must
//   preserve the values of callee-saves registers; if it uses them,
//   it must save their original values somewhere, and restore them
//   before it returns.  In contrast, the callee is free to trash
//   caller-saves registers; if the callee uses these, it will
//   probably not bother to save them anywhere, and the CFI will
//   probably mark their values as "unrecoverable".
// 
// - Exactly where the CFA points is a matter of convention that
//   depends on the architecture and ABI in use.  Perhaps it points at
//   the return address, perhaps elsewhere.  But by definition, the
//   CFA remains constant throughout the lifetime of the frame.  This
//   makes it a useful value for other columns to refer to.
//
// If you look at the table above, you'll notice that a given entry is
// often the same as the one immediately above it: most instructions
// change only one or two aspects of the stack frame, if they affect
// it at all.  In the actual DWARF data, we take advantage of this
// fact, and compress the representation of the table by mentioning
// only the addresses and columns at which changes take place.  So for
// the above, the DWARF CFI data would only actually mention the
// following:
// 
//     insn      cfa    r0      r1 ...  ra
//     =======================================
//     func+0:   sp                     cfa[0]
//     func+1:   sp+16
//     func+2:          cfa[-4]
//     func+11:  sp+20
//     func+21:         r0
//     func+22:  sp            
//
// In fact, this is the way the parser reports CFI to the consumer: as
// a series of statements of the form, "At address X, column Y changed
// to Z," and related conventions for describing the initial state.
//
// Naturally, it would be impractical to have to scan the entire
// program's CFI just to recover one instruction's unwinding rules, so
// the CFI data is typically grouped into entries, each covering a
// single function, which begin with a complete statement of the rules
// for all recoverable registers.
//
// Thus, to compute the contents of a given row of the table --- that
// is, rules for recovering the CFA, RA, and registers at a given
// instruction --- the consumer should start at the the initial state
// supplied at the beginning of the entry, and work forward until it
// has processed all the changes up to and including those for the
// present instruction.
//
// There are three kinds of values that can appear in an entry of the
// table:
//
// - "undefined": The given register is not preserved by the callee;
//   its value cannot be recovered.
//
// - "same value": This register has the same value it did in the callee.
//
// - EXPRESSION: Evaluating the given expression yields the register's
//   value.  We represent expressions in the little postfix language
//   used by Breakpad, described in the comments in file
//   src/processor/postfix_evaluator.h.

class CallFrameInfo {

  // Create a DWARF CFI parser.  BUFFER points to the contents of the
  // .debug_frame section to parse; BUFFER_LENGTH is its length in
  // bytes.  Reader is a ByteReader instance that has the endianness
  // set properly.  Report the data we find to HANDLER.  Use
  // REGISTER_NAMES[R] as the name of register number R in the postfix
  // expressions we pass to HANDLER.
  CallFrameInfo(const uint8 *buffer, size_t buffer_length,
                const vector<const string> &register_names,
                CallFrameInfoHandler *handler):
      buffer_(buffer),
      buffer_length_(buffer_length),
      register_names_(register_names),
      handler_(handler) { }
  ~CallFrameInfo();

  // Parse the entries in BUFFER, reporting what we find to HANDLER.
  // Return true if we reach the end of the section successfully, or
  // false if we encounter an error.
  bool Start();

 private:

  // The contents of the DWARF .debug_info section we're parsing.
  const uint8 *buffer_;
  size_t buffer_length_;

  // The names we should use to refer to registers in postfix expressions.
  const vector<const string> &register_names_;

  // The handler to which we should report the data we find.
  CallFrameInfoHandler *handler_;
};

// The handler class for CallFrameInfo.  The a CFI parser calls the
// member functions of a handler object to report the data it finds.
class CallFrameInfoHandler {
  CallFrameInfoHandler() {}
  virtual ~CallFrameInfoHandler();

  // The parser has found CFI for the machine code at ADDRESS,
  // extending for LENGTH bytes.  VERSION is the version number of the
  // CFI format.  AUGMENTATION is a string describing any
  // producer-specific extensions present in the data.  RETURN_ADDRESS
  // is the number of the register that holds the address to which the
  // function should return.
  //
  // Entry should return true to process this CFI, or false to skip to
  // the next entry.
  //
  // The parser invokes Entry for each Frame Description Entry (FDE)
  // it finds.  The parser doesn't report Common Information Entries
  // to the handler explicitly; instead, if the handler elects to
  // process a given FDE, the parser reiterates the appropriate CIE's
  // contents at the beginning of the FDE's rules.
  virtual bool Entry(uint64 address, uint64 length, uint8 version,
                     const char *augmentation, int return_address) = 0;

  // When the Entry function returns true, the parser calls this
  // handler function repeatedly to describe the rules for recovering
  // registers at each instruction in the given range of machine code.
  // Immediately after a call to Entry, the handler should assume that
  // the rule for each register is "undefined" --- that is, that the
  // register's value cannot be recovered.
  //
  // At ADDRESS, use RULE to recover register number COLUMN.  RULE is
  // either:
  //
  // - the string "undefined", meaning that value of the register in the
  //   caller cannot be recovered at this point,
  //
  // - the string "same", meaning that the callee has not changed the
  //   value of the register, and it still holds the value it had in
  //   the caller, or
  //
  // - a postfix expression that computes the value of the register.
  //   This expression may refer to the CFA using the identifier
  //   "$cfa", and may refer to other registers using the names
  //   provided by the RegisterName member function, below.
  //
  // Rule should return true if everything is okay, or false if
  // an error occurs and parsing should stop.
  virtual bool Rule(uint64 address, int column, const string &rule) = 0;
};

}  // namespace dwarf2reader

#endif  // UTIL_DEBUGINFO_DWARF2READER_H__
