#include "argumentlist.h"

#include "basictypeio.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

static int align(uint32 index, uint32 alignment)
{
    const int maxStepUp = alignment - 1;
    return (index + maxStepUp) & ~maxStepUp;
}

// helper to verify the max nesting requirements of the d-bus spec
struct Nesting
{
    Nesting() : array(0), paren(0), variant(0) {}
    static const int arrayMax = 32;
    static const int parenMax = 32;
    static const int totalMax = 64;

    bool beginArray() { array++; return array <= arrayMax && total() <= totalMax; }
    void endArray() { array--; }
    bool beginParen() { paren++; return paren <= parenMax && total() <= totalMax; }
    void endParen() { paren--; }
    bool beginVariant() { variant++; return total() <= totalMax; }
    void endVariant() { variant--; }
    int total() { return array + paren + variant; }

    int array;
    int paren;
    int variant;
};

static cstring printableState(ArgumentList::CursorState state)
{
    if (state < ArgumentList::NotStarted || state > ArgumentList::UnixFd) {
        return cstring();
    }
    static const char *strings[ArgumentList::UnixFd + 1] = {
        "NotStarted",
        "Finished",
        "NeedMoreData",
        "InvalidData",
        "AnyData",
        "DictKey",
        "BeginArray",
        "NextArrayEntry",
        "EndArray",
        "BeginDict",
        "NextDictEntry",
        "EndDict",
        "BeginStruct",
        "EndStruct",
        "BeginVariant",
        "EndVariant",
        "Byte",
        "Boolean",
        "Int16",
        "Uint16",
        "Int32",
        "Uint32",
        "Int64",
        "Uint64",
        "Double",
        "String",
        "ObjectPath",
        "Signature",
        "UnixFd"
    };
    return cstring(strings[state]);
}

ArgumentList::ArgumentList()
   : m_isByteSwapped(false),
     m_readCursorCount(0),
     m_hasWriteCursor(false)
{
}

ArgumentList::ArgumentList(cstring signature, array data, bool isByteSwapped)
   : m_isByteSwapped(isByteSwapped),
     m_readCursorCount(0),
     m_hasWriteCursor(false),
     m_signature(signature),
     m_data(data)
{
}

ArgumentList::ReadCursor ArgumentList::beginRead()
{
    ArgumentList *thisInstance = 0;
    if (!m_hasWriteCursor) {
        m_readCursorCount++;
        thisInstance = this;
    }
    return ReadCursor(thisInstance);
}

ArgumentList::WriteCursor ArgumentList::beginWrite()
{
    ArgumentList *thisInstance = 0;
    if (!m_readCursorCount && !m_hasWriteCursor) {
        m_hasWriteCursor = true;
        thisInstance = this;
    }
    return WriteCursor(thisInstance);
}

static void chopFirst(cstring *s)
{
    s->begin++;
    s->length--;
}

// static
bool ArgumentList::isStringValid(cstring string)
{
    if (!string.begin || string.begin[string.length] != 0) {
        return false;
    }
    for (int i = 0; i < string.length; i++) {
        if (string.begin[i] == 0) {
            return false;
        }
    }
    return true;
}

static bool isObjectNameLetter(byte b)
{
    return (b >= 'a' && b <= 'z') || b == '_' || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9');
}

// static
bool ArgumentList::isObjectPathValid(cstring path)
{
    if (!path.begin || path.begin[path.length] != 0) {
        return false;
    }
    byte lastLetter = path.begin[0];
    if (lastLetter != '/') {
        return false;
    }
    if (path.length == 1) {
        return true; // "/" special case
    }
    for (int i = 1; i < path.length; i++) {
        byte currentLetter = path.begin[i];
        if (lastLetter == '/') {
            if (!isObjectNameLetter(currentLetter)) {
                return false;
            }
        } else {
            if (currentLetter != '/' && !isObjectNameLetter(currentLetter)) {
                return false;
            }
        }
        lastLetter = currentLetter;
    }
    return lastLetter != '/';
}

static bool parseBasicType(cstring *s)
{
    // ### not checking if zero-terminated
    assert(s->begin);
    if (s->length < 0) {
        return false;
    }
    switch (*s->begin) {
    case 'y':
    case 'b':
    case 'n':
    case 'q':
    case 'i':
    case 'u':
    case 'x':
    case 't':
    case 'd':
    case 's':
    case 'o':
    case 'g':
    case 'h':
        chopFirst(s);
        return true;
    default:
        return false;
    }
}

static bool parseSingleCompleteType(cstring *s, Nesting *nest)
{
    assert(s->begin);
    // ### not cheching if zero-terminated

    switch (*s->begin) {
    case 'y':
    case 'b':
    case 'n':
    case 'q':
    case 'i':
    case 'u':
    case 'x':
    case 't':
    case 'd':
    case 's':
    case 'o':
    case 'g':
    case 'h':
        chopFirst(s);
        return true;
    case 'v':
        if (!nest->beginVariant()) {
            return false;
        }
        chopFirst(s);
        nest->endVariant();
        return true;
    case '(': {
        if (!nest->beginParen()) {
            return false;
        }
        chopFirst(s);
        bool isEmptyStruct = true;
        while (parseSingleCompleteType(s, nest)) {
            isEmptyStruct = false;
        }
        if (!s->length || *s->begin != ')' || isEmptyStruct) {
            return false;
        }
        chopFirst(s);
        nest->endParen();
        return true; }
    case 'a':
        if (!nest->beginArray()) {
            return false;
        }
        chopFirst(s);
        if (*s->begin == '{') { // an "array of dict entries", i.e. a dict
            if (!nest->beginParen() || s->length < 4) {
                return false;
            }
            chopFirst(s);
            // key must be a basic type
            if (!parseBasicType(s)) {
                return false;
            }
            // value can be any type
            if (!parseSingleCompleteType(s, nest)) {
                return false;
            }
            if (!s->length || *s->begin != '}') {
                return false;
            }
            chopFirst(s);
            nest->endParen();
        } else { // regular array
            if (!parseSingleCompleteType(s, nest)) {
                return false;
            }
        }
        nest->endArray();
        return true;
    default:
        return false;
    }
}

//static
bool ArgumentList::isSignatureValid(cstring signature, SignatureType type)
{
    Nesting nest;
    if (!signature.begin || signature.begin[signature.length] != 0) {
        return false;
    }
    if (type == VariantSignature) {
        if (signature.length && !parseSingleCompleteType(&signature, &nest)) {
            return false;
        }
        if (signature.length) {
            return false;
        }
    } else {
        while (signature.length) {
            if (!parseSingleCompleteType(&signature, &nest)) {
                return false;
            }
        }
    }
    // all aggregates must be closed at the end; if those asserts trigger the parsing code is not correct
    assert(!nest.array);
    assert(!nest.paren);
    assert(!nest.variant);
    return true;
}

ArgumentList::ReadCursor::ReadCursor(ArgumentList *al)
   : m_argList(al),
     m_state(NotStarted),
     m_nesting(new Nesting),
     m_signaturePosition(-1),
     m_dataPosition(0),
     m_zeroLengthArrayNesting(0)
{
    if (m_argList) {
        m_signature = m_argList->m_signature;
        m_data = m_argList->m_data;
        if (!ArgumentList::isSignatureValid(m_signature)) {
            m_state = InvalidData;
        }
        advanceState();
    } else {
        m_state = InvalidData;
    }
}

ArgumentList::ReadCursor::~ReadCursor()
{
    if (m_argList) {
        m_argList->m_readCursorCount -= 1;
    }
    delete m_nesting;
    m_nesting = 0;
}

cstring ArgumentList::ReadCursor::stateString() const
{
    return printableState(m_state);
}

void ArgumentList::ReadCursor::replaceData(array data)
{
    m_data = data;
}

static void getTypeInfo(byte letterCode, ArgumentList::CursorState *typeState, uint32 *alignment,
                        bool *isPrimitiveType, bool *isStringType)
{
    ArgumentList::CursorState state = ArgumentList::InvalidData;
    bool isPrimitive = true;
    bool isString = false;
    int align = 4;
    // TODO use table lookup instead
    switch (letterCode) {
    case 'y':
        state = ArgumentList::Byte;
        align = 1;
        break;
    case 'b':
        state = ArgumentList::Boolean;
        break;
    case 'n':
        state = ArgumentList::Int16;
        align = 2;
        break;
    case 'q':
        state = ArgumentList::Uint16;
        align = 2;
        break;
    case 'i':
        state = ArgumentList::Int32;
        break;
    case 'u':
        state = ArgumentList::Uint32;
        break;
    case 'x':
        state = ArgumentList::Int64;
        align = 8;
        break;
    case 't':
        state = ArgumentList::Uint64;
        align = 8;
        break;
    case 'd':
        state = ArgumentList::Double;
        align = 8;
        break;
    case 's':
        state = ArgumentList::String;
        isPrimitive = false;
        isString = true;
        break;
    case 'o':
        state = ArgumentList::ObjectPath;
        isPrimitive = false;
        isString = true;
        break;
    case 'g':
        state = ArgumentList::Signature;
        isPrimitive = false;
        isString = true;
        align = 1;
        break;
    case 'h':
        state = ArgumentList::UnixFd;
        // this is handled like a primitive type with some extra postprocessing
        break;
    case 'v':
        state = ArgumentList::BeginVariant;
        isPrimitive = false;
        align = 1;
        break;
    case '(':
        state = ArgumentList::BeginStruct;
        isPrimitive = false;
        align = 8;
        break;
    case ')':
        state = ArgumentList::EndStruct;
        isPrimitive = false;
        align = 1;
        break;
    case 'a':
        state = ArgumentList::BeginArray;
        isPrimitive = false;
        break;
    case '{':
        state = ArgumentList::BeginDict;
        isPrimitive = false;
        align = 8;
        break;
    case '}':
        state = ArgumentList::EndDict;
        isPrimitive = false;
        align = 1;
        break;
    default:
        align = 1; // don't move the data read/write pointer by aligning it
        break;
    }
    if (typeState) {
        *typeState = state;
    }
    if (alignment) {
        *alignment = align;
    }
    if (isPrimitiveType) {
        *isPrimitiveType = isPrimitive;
    }
    if (isStringType) {
        *isStringType = isString;
    }
}

ArgumentList::CursorState ArgumentList::ReadCursor::doReadPrimitiveType()
{
    switch(m_state) {
    case Byte:
        m_Byte = m_data.begin[m_dataPosition];
        break;
    case Boolean: {
        uint32 num = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        m_Boolean = num == 1;
        if (num > 1) {
            return InvalidData;
        }
        break; }
    case Int16:
        m_Int16 = basic::readInt16(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        break;
    case Uint16:
        m_Uint16 = basic::readUint16(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        break;
    case Int32:
        m_Int32 = basic::readInt32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        break;
    case Uint32:
        m_Uint32 = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        break;
    case Int64:
        m_Int64 = basic::readInt64(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        break;
    case Uint64:
        m_Uint64 = basic::readUint64(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        break;
    case Double:
        m_Double = basic::readDouble(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        break;
    case UnixFd: {
        uint32 index = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        uint32 ret; // TODO use index to retrieve the actual file descriptor
        m_Uint32 = ret;
        break; }
    default:
        assert(false);
        return InvalidData;
    }
    return m_state;
}

ArgumentList::CursorState ArgumentList::ReadCursor::doReadString(int lengthPrefixSize)
{
    uint32 stringLength = 1; // terminating nul
    if (lengthPrefixSize == 1) {
        stringLength += m_data.begin[m_dataPosition];
    } else {
        stringLength += basic::readUint32(m_data.begin + m_dataPosition,
                                          m_argList->m_isByteSwapped);
    }
    m_dataPosition += lengthPrefixSize;
    if (m_dataPosition + stringLength > m_data.length) {
        return NeedMoreData;
    }
    m_String.begin = m_data.begin + m_dataPosition;
    m_String.length = stringLength;
    m_dataPosition += stringLength;
    bool isValidString = false;
    if (m_state == String) {
        isValidString = ArgumentList::isStringValid(cstring(m_String.begin, m_String.length));
    } else if (m_state == ObjectPath) {
        isValidString = ArgumentList::isObjectPathValid(cstring(m_String.begin, m_String.length));
    } else if (m_state == Signature) {
        isValidString = ArgumentList::isSignatureValid(cstring(m_String.begin, m_String.length));
    }
    if (!isValidString) {
        return InvalidData;
    }
    return m_state;
}

void ArgumentList::ReadCursor::advanceState()
{
    // TODO: when inside any array, we may never run out of data because the array length is given
    //       in the data stream and we won't start the array until its data is complete.

    // if we don't have enough data, the strategy is to keep everything unchanged
    // except for the state which will be NeedMoreData
    // we don't have to deal with invalid signatures here because they are checked beforehand EXCEPT
    // for aggregate nesting which cannot be checked using only one signature, due to variants.
    // variant signatures are only parsed while reading the data. individual variant signatures
    // ARE checked beforehand whenever we find one in this method.

    if (m_state == InvalidData) { // nonrecoverable...
        return;
    }

    assert(m_signaturePosition < m_signature.length);

    const int savedSignaturePosition = m_signaturePosition;
    const int savedDataPosition = m_dataPosition;

    // how to recognize end of...
    // - array entry: BeginArray at top of aggregate stack and we're not at the beginning
    //                of the array (so we've just finished reading a single complete type)
    // - dict entry: BeginDict at top of aggregate stack and the current type signature char is '}'
    // - array: at/past end of array data; end of array entry condition must hold, too
    // - dict: at/past end of dict data; end of dict entry conditions must hold, too
    // - struct: BeginStruct at top of aggregate stack and current char = ')'
    // - variant: BeginVariant at top of aggregate stack and at end of type signature
    // - argument list: aggregate stack is empty and at end of type signature

    // check if we are about to close any aggregate or even the whole argument list

    m_signaturePosition++;

    if (m_aggregateStack.empty()) {
        if (m_signaturePosition >= m_signature.length) {
            m_state = Finished;
            return;
        }
    } else {
        const AggregateInfo &aggregateInfo = m_aggregateStack.back();
        switch (aggregateInfo.aggregateType) {
        case BeginStruct:
            break; // handled later by getTypeInfo recognizing ')' -> EndStruct
        case BeginVariant:
            if (m_signaturePosition >= m_signature.length) {
                m_state = EndVariant;
                m_nesting->endVariant();
                m_signature.begin = aggregateInfo.var.prevSignature.begin;
                m_signature.length = aggregateInfo.var.prevSignature.length;
                m_signaturePosition = aggregateInfo.var.prevSignaturePosition + 1;
                m_aggregateStack.pop_back();
                return;
            }
            break;
        case BeginDict:
            // fall through
        case BeginArray: {
            const bool isDict = aggregateInfo.aggregateType == BeginDict;
            const bool isEndOfEntry = isDict ? (m_signature.begin[m_signaturePosition] == '}')
                                             : (m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1);
            if (isEndOfEntry) {
                m_state = isDict ? NextDictEntry : NextArrayEntry;
                return; // the rest is handled in nextArrayOrDictEntry()
            } else {
                const bool isEndOfData = m_dataPosition >= aggregateInfo.arr.dataEnd;
                if (isEndOfData) {
                    m_state = InvalidData;
                    return;
                }
            }
            break; }
        default:
            break;
        }
    }

    // for aggregate types, it's just the alignment. for primitive types, it's also the actual size.
    uint32 alignment = 1;
    bool isPrimitiveType = false;
    bool isStringType = false;

    getTypeInfo(m_signature.begin[m_signaturePosition],
                &m_state, &alignment, &isPrimitiveType, &isStringType);

    if (m_state == InvalidData) {
        return;
    }


    // check if we have enough data for the next type, and read it
    // if we're in a zero-length array, we are iterating only over the types without reading any data

    if (m_zeroLengthArrayNesting && (isPrimitiveType || isStringType)) {
        return; // nothing to do
    }

    m_dataPosition = align(m_dataPosition, alignment);

    if (((isPrimitiveType || isStringType) && m_dataPosition + alignment > m_data.length)
        || m_dataPosition > m_data.length) {
        goto out_needMoreData;
    }

    if (isPrimitiveType) {
        m_state = doReadPrimitiveType();
        m_dataPosition += alignment;
        return;
    }

    if (isStringType) {
        m_state = doReadString(alignment);
        if (m_state == NeedMoreData) {
            goto out_needMoreData;
        }
        return;
    }

    // now the interesting part: aggregates

    AggregateInfo aggregateInfo;

    switch (m_state) {
    case BeginStruct:
        if (!m_nesting->beginParen()) {
            m_state = InvalidData;
            return;
        }
        aggregateInfo.aggregateType = BeginStruct;
        m_aggregateStack.push_back(aggregateInfo);
        break;
    case EndStruct:
        m_nesting->endParen();
        if (!m_aggregateStack.size() || m_aggregateStack.back().aggregateType != BeginStruct) {
            assert(false); // should never happen due to the pre-validated signature
        }
        m_aggregateStack.pop_back();
        break;

    case BeginVariant: {
        if (m_dataPosition >= m_data.length) {
            goto out_needMoreData;
        }
        cstring signature;
        if (m_zeroLengthArrayNesting) {
            static const char *emptyString = "";
            signature = cstring(emptyString, 1);
        } else {
            signature.length = m_data.begin[m_dataPosition++] + 1;
            signature.begin = m_data.begin + m_dataPosition;
            m_dataPosition += signature.length;
            if (m_dataPosition > m_data.length) {
                goto out_needMoreData;
            }
        }
        // do not clobber nesting before potentially going to out_needMoreData!
        if (!m_nesting->beginVariant()) {
            m_state = InvalidData;
            return;
        }

        if (!ArgumentList::isSignatureValid(signature, ArgumentList::VariantSignature)) {
            m_state = InvalidData;
            return;
        }

        aggregateInfo.aggregateType = BeginVariant;
        aggregateInfo.var.prevSignature.begin = m_signature.begin;
        aggregateInfo.var.prevSignature.length = m_signature.length;
        aggregateInfo.var.prevSignaturePosition = m_signaturePosition;
        m_aggregateStack.push_back(aggregateInfo);
        m_signature = signature;
        m_signaturePosition = -1; // because we increment m_signaturePosition before reading a char
        break; }

    case BeginArray: {
        uint32 arrayLength = 0;
        if (!m_zeroLengthArrayNesting) {
            if (m_dataPosition + 4 > m_data.length) {
                goto out_needMoreData;
            }
            static const int maxArrayDataLength = 67108864; // from the spec
            arrayLength = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            if (arrayLength > maxArrayDataLength) {
                m_state = InvalidData;
                return;
            }
            m_dataPosition += 4;
        }

        CursorState firstElementType;
        uint32 firstElementAlignment;
        getTypeInfo(m_signature.begin[m_signaturePosition + 1],
                    &firstElementType, &firstElementAlignment, 0, 0);

        m_state = firstElementType == BeginDict ? BeginDict : BeginArray;
        aggregateInfo.aggregateType = m_state;

        // ### are we supposed to align m_dataPosition if the array is empty?
        if (!m_zeroLengthArrayNesting) {
            m_dataPosition = align(m_dataPosition, firstElementAlignment);
        }
        aggregateInfo.arr.dataEnd = m_dataPosition + arrayLength;
        if (aggregateInfo.arr.dataEnd > m_data.length) {
            // NB: do not clobber (the unsaved) nesting before potentially going to out_needMoreData!
            goto out_needMoreData;
        }
        bool nestOk = m_nesting->beginArray();
        if (firstElementType == BeginDict) {
            m_signaturePosition++;
            nestOk = nestOk && m_nesting->beginParen();
        }
        if (!nestOk) {
            m_state = InvalidData;
            return;
        }

        // position at the 'a' or '{' because we increment m_signaturePosition before reading a char
        aggregateInfo.arr.containedTypeBegin = m_signaturePosition;
        if (!arrayLength) {
            m_zeroLengthArrayNesting++;
        }

        m_aggregateStack.push_back(aggregateInfo);
        break; }

    default:
        assert(false);
        break;
    }

    return;

out_needMoreData:
    m_state = NeedMoreData;
    if (m_nesting->array) {
        // we only start an array when the data for it has fully arrived (possible due to the length
        // prefix), so if we still run out of data in an array the input is inconsistent.
        m_state = InvalidData;
    }
    m_signaturePosition = savedSignaturePosition;
    m_dataPosition = savedDataPosition;
}

void ArgumentList::ReadCursor::advanceStateFrom(CursorState expectedState)
{
    if (m_state == expectedState) {
        advanceState();
    } else {
        m_state = InvalidData;
    }
}

void ArgumentList::ReadCursor::beginArrayOrDict(bool isDict, bool *isEmpty)
{
    assert(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));

    if (isEmpty) {
        *isEmpty = m_zeroLengthArrayNesting;
    }

    if (m_zeroLengthArrayNesting) {
        if (!isEmpty) {
            // need to move m_signaturePosition to the end of the array signature or it won't happen
            cstring temp(m_signature.begin + m_signaturePosition, m_signature.length - m_signaturePosition);
            // fix up nesting before and after we re-parse the beginning of the array signature
            if (isDict) {
                m_nesting->endParen();
                m_signaturePosition--; // it was moved ahead by one to skip the '{'
            }
            m_nesting->endArray();
            if (!parseSingleCompleteType(&temp, m_nesting)) {
                // must have been too deep nesting (assuming no bugs)
                m_state = InvalidData;
                return;
            }
            m_nesting->beginArray();
            if (isDict) {
                m_nesting->beginParen();
            }
            m_signaturePosition = m_signature.length - temp.length - 1; // TODO check/fix the indexing
        }
    }
    m_state = isDict ? NextDictEntry : NextArrayEntry;
}

// TODO introduce an error state different from InvalidData when the wrong method is called
void ArgumentList::ReadCursor::beginArray(bool *isEmpty)
{
    if (m_state == BeginArray) {
        beginArrayOrDict(false, isEmpty);
    } else {
        m_state = InvalidData;
    }
}

bool ArgumentList::ReadCursor::nextArrayOrDictEntry(bool isDict)
{
    assert(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));

    if (m_zeroLengthArrayNesting) {
        if (m_signaturePosition <= aggregateInfo.arr.containedTypeBegin) {
            // do one iteration to read the types
            return true;
        } else {
            // second iteration or skipping an empty array
            m_zeroLengthArrayNesting--;
        }
    } else {
        if (m_dataPosition < aggregateInfo.arr.dataEnd) {
            // rewind to start of contained type and read the data there
            m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
            advanceState();
            return m_state != InvalidData;
        }
    }
    // no more iterations
    m_state = isDict ? EndDict : EndArray;
    m_signaturePosition--; // this was increased in advanceState() before sending us here
    if (isDict) {
        m_nesting->endParen();
        m_signaturePosition++; // skip '}'
    }
    m_nesting->endArray();
    m_aggregateStack.pop_back();
    return false;
}

bool ArgumentList::ReadCursor::nextArrayEntry()
{
    if (m_state == NextArrayEntry) {
        return nextArrayOrDictEntry(false);
    } else {
        m_state = InvalidData;
        return false;
    }
}

void ArgumentList::ReadCursor::endArray()
{
    advanceStateFrom(EndArray);
}

bool ArgumentList::ReadCursor::beginDict(bool *isEmpty)
{
    if (m_state == BeginDict) {
        beginArrayOrDict(true, isEmpty);
    } else {
        m_state = InvalidData;
    }
}

bool ArgumentList::ReadCursor::nextDictEntry()
{
    if (m_state == NextDictEntry) {
        return nextArrayOrDictEntry(true);
    } else {
        m_state = InvalidData;
        return false;
    }
}

bool ArgumentList::ReadCursor::endDict()
{
    advanceStateFrom(EndDict);
}

bool ArgumentList::ReadCursor::beginStruct()
{
    advanceStateFrom(BeginStruct);
}

bool ArgumentList::ReadCursor::endStruct()
{
    advanceStateFrom(EndStruct);
}

bool ArgumentList::ReadCursor::beginVariant()
{
    advanceStateFrom(BeginVariant);
}

bool ArgumentList::ReadCursor::endVariant()
{
    advanceStateFrom(EndVariant);
}

std::vector<ArgumentList::CursorState> ArgumentList::ReadCursor::aggregateStack() const
{
    const int count = m_aggregateStack.size();
    std::vector<CursorState> ret;
    for (int i = 0; i < count; i++) {
        ret.push_back(m_aggregateStack[i].aggregateType);
    }
    return ret;
}

ArgumentList::WriteCursor::WriteCursor(ArgumentList *al)
   : m_argList(al),
     m_state(AnyData),
     m_nesting(new Nesting),
     m_signature(reinterpret_cast<byte *>(malloc(maxSignatureLength + 1)), 0),
     m_signaturePosition(0),
     m_data(reinterpret_cast<byte *>(malloc(16384)), 0),
     m_dataPosition(0),
     m_zeroLengthArrayNesting(0)
{
}

ArgumentList::WriteCursor::~WriteCursor()
{
    if (m_argList) {
        assert(m_argList->m_hasWriteCursor);
        m_argList->m_hasWriteCursor = false;
    }
    delete m_nesting;
    m_nesting = 0;
}

cstring ArgumentList::WriteCursor::stateString() const
{
    return printableState(m_state);
}

ArgumentList::CursorState ArgumentList::WriteCursor::doWritePrimitiveType(uint32 alignAndSize)
{
    switch(m_state) {
    case Byte:
        m_data.begin[m_dataPosition] = m_Byte;
        break;
    case Boolean: {
        uint32 num = m_Boolean ? 1 : 0;
        basic::writeUint32(m_data.begin + m_dataPosition, num);
        break; }
    case Int16:
        basic::writeInt16(m_data.begin + m_dataPosition, m_Int16);
        break;
    case Uint16:
        basic::writeUint16(m_data.begin + m_dataPosition, m_Uint16);
        break;
    case Int32:
        basic::writeInt32(m_data.begin + m_dataPosition, m_Int32);
        break;
    case Uint32:
        basic::writeUint32(m_data.begin + m_dataPosition, m_Uint32);
        break;
    case Int64:
        basic::writeInt64(m_data.begin + m_dataPosition, m_Int64);
        break;
    case Uint64:
        basic::writeUint64(m_data.begin + m_dataPosition, m_Uint64);
        break;
    case Double:
        basic::writeDouble(m_data.begin + m_dataPosition, m_Double);
        break;
    case UnixFd: {
        uint32 index; // TODO = index of the FD we actually want to send
        basic::writeUint32(m_data.begin + m_dataPosition, index);
        break; }
    default:
        assert(false);
        return InvalidData;
    }

    m_dataPosition += alignAndSize;
    m_elements.push_back(ElementInfo(alignAndSize, alignAndSize));
    return m_state;
}

ArgumentList::CursorState ArgumentList::WriteCursor::doWriteString(int lengthPrefixSize)
{
    // TODO request more data when we'd overflow the output buffer

    bool isValidString = false;
    if (m_state == String) {
        isValidString = ArgumentList::isStringValid(cstring(m_String.begin, m_String.length));
    } else if (m_state == ObjectPath) {
        isValidString = ArgumentList::isObjectPathValid(cstring(m_String.begin, m_String.length));
    } else if (m_state == Signature) {
        isValidString = ArgumentList::isSignatureValid(cstring(m_String.begin, m_String.length));
    }
    if (!isValidString) {
        return InvalidData;
    }

    if (lengthPrefixSize == 1) {
        m_data.begin[m_dataPosition] = m_String.length;
    } else {
        basic::writeUint32(m_data.begin + m_dataPosition, m_String.length);
    }
    m_dataPosition += lengthPrefixSize;
    memcpy(m_data.begin + m_dataPosition, m_String.begin, m_String.length);
    m_dataPosition += m_String.length;

    m_elements.push_back(ElementInfo(lengthPrefixSize, lengthPrefixSize));
    for (uint32 l = m_String.length; l; ) {
        uint32 chunkSize = std::min(l, uint32(ElementInfo::LargestSize));
        m_elements.push_back(ElementInfo(1, chunkSize));
        l -= chunkSize;
    }

    return m_state;
}

void ArgumentList::WriteCursor::advanceState(array signatureFragment, CursorState newState)
{
// Macros are really ugly, but here every use saves three lines...
#define VALID_IF(cond) if (cond) {} else { m_state = InvalidData; return; } do {} while (0)

    // what needs to happen here:
    // - if we are in an existing portion of the signature (like writing the >1st iteration of an array)
    //   check if the type to be written is the same as the one that's already in the signature
    //   - otherwise we still need to check if the data we're adding conforms with the spec, e.g.
    //     no empty structs, dict entries must have primitive key type and exactly one value type
    // - check well-formedness of data: strings, maximum lengths in binary format
    //   (due to variant length only being known after finishing it and the need to insert it into
    //    the data stream, keeping track of length is more difficult than just using the current
    //    write position in the output buffer)
    // - increase size of data buffer when it gets too small
    // - keep track of variants in some data structure, in order to:
    //   - know what the final binary message size will be
    //   - in finish(), create the final data stream with inline variant signatures

    // TODO
    // if the innermost aggregate is...
    // - array: first iteration: write type and data, or if empty just write type
    //   higher iterations: check type and write data
    // - variant: write type (to special variant signature storage) and data; check type is single complete

    // TODO extend m_data before it overflows!

    if (m_state == InvalidData) {
        return;
    }

    m_state = newState;
    uint32 alignment = 1;
    bool isPrimitiveType = false;
    bool isStringType = false;

    if (signatureFragment.length) {
        getTypeInfo(signatureFragment.begin[0], 0, &alignment, &isPrimitiveType, &isStringType);
    }

    // TODO review all the signature indexing
    bool isWritingSignature = m_signaturePosition == m_signature.length; // TODO correct?
    if (isWritingSignature) {
        // signature additions must conform to syntax
        VALID_IF(m_signaturePosition + signatureFragment.length <= maxSignatureLength);

        if (!m_aggregateStack.empty()) {
            const AggregateInfo &aggregateInfo = m_aggregateStack.back();
            switch (aggregateInfo.aggregateType) {
            case BeginVariant:
                // arrays and variants may contain just one single complete type (note that this will
                // trigger only when not inside an aggregate inside the variant or array)
                if (m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1) {
                    VALID_IF(m_state == EndVariant);
                }
                break;
            case BeginArray:
                if (m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1) {
                    VALID_IF(m_state == EndArray);
                }
                break;
            case BeginDict:
                if (m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
                    VALID_IF(isPrimitiveType || isStringType);
                }
                // first type has been checked already, second must be present (checked in EndDict
                //  state handler). no third type allowed.
                if (m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 2) { // TODO correct indexing
                    VALID_IF(m_state == EndDict);
                }
                break;
            default:
                break;
            }
        }

        // finally, extend the signature
        for (int i = 0; i < signatureFragment.length; i++) {
            m_signature.begin[m_signaturePosition++] = signatureFragment.begin[i];
        }
        m_signature.length += signatureFragment.length;
    } else {
        // signature must match first iteration (of an array/dict)
        VALID_IF(m_signaturePosition + signatureFragment.length <= m_signature.length);
        // TODO need to apply special checks for state changes with no explicit signature char?
        // (end of array, end of variant and such)
        for (int i = 0; i < signatureFragment.length; i++) {
            VALID_IF(m_signature.begin[m_signaturePosition++] == signatureFragment.begin[i]);
        }
    }

    if (isPrimitiveType) {
        m_state = doWritePrimitiveType(alignment);
        return;
    }
    if (isStringType) {
        m_state = doWriteString(alignment);
        return;
    }

    AggregateInfo aggregateInfo;

    switch (m_state) {
    case BeginStruct:
        VALID_IF(m_nesting->beginParen());
        aggregateInfo.aggregateType = BeginStruct;
        aggregateInfo.sct.containedTypeBegin = m_signaturePosition;
        m_aggregateStack.push_back(aggregateInfo);
        m_elements.push_back(ElementInfo(8, 0)); // align only
        break;
    case EndStruct:
        m_nesting->endParen();
        VALID_IF(!m_aggregateStack.empty());
        aggregateInfo = m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginStruct &&
                 m_signaturePosition > aggregateInfo.sct.containedTypeBegin + 1); // no empty structs
        m_aggregateStack.pop_back();
        break;

    case BeginVariant: {
        VALID_IF(m_nesting->beginVariant());
        aggregateInfo.aggregateType = BeginVariant;
        aggregateInfo.var.prevSignature.begin = m_signature.begin;
        aggregateInfo.var.prevSignature.length = m_signature.length;
        aggregateInfo.var.prevSignaturePosition = m_signaturePosition;
        aggregateInfo.var.signatureIndex = m_variantSignatures.size();
        m_aggregateStack.push_back(aggregateInfo);

        // arrange for finish() to take a signature from m_variantSignatures
        m_elements.push_back(ElementInfo(1, ElementInfo::VariantSignature));
        cstring str(reinterpret_cast<byte *>(malloc(maxSignatureLength + 1)), 0);
        m_variantSignatures.push_back(str);
        m_signature = str;
        m_signaturePosition = 0;
        break; }
    case EndVariant: {
        m_nesting->endVariant();
        VALID_IF(!m_aggregateStack.empty());
        aggregateInfo = m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginVariant);
        m_signature.begin[m_signaturePosition++] = '\0';
        assert(aggregateInfo.var.signatureIndex < m_variantSignatures.size());
        m_variantSignatures[aggregateInfo.var.signatureIndex].length = m_signaturePosition;
        assert(m_variantSignatures[aggregateInfo.var.signatureIndex].begin = m_signature.begin);

        m_signature.begin = aggregateInfo.var.prevSignature.begin;
        m_signature.length = aggregateInfo.var.prevSignature.length;
        m_signaturePosition = aggregateInfo.var.prevSignaturePosition;
        m_aggregateStack.pop_back();
        break; }

    case BeginDict:
    case BeginArray: {
        VALID_IF(m_nesting->beginVariant());
        if (m_state == BeginDict) {
            VALID_IF(m_nesting->beginParen());
        }
        aggregateInfo.aggregateType = m_state;
        aggregateInfo.arr.dataBegin = m_dataPosition;
        aggregateInfo.arr.containedTypeBegin = m_signaturePosition;
        m_aggregateStack.push_back(aggregateInfo);

        m_elements.push_back(ElementInfo(4, ElementInfo::ArrayLengthField));
        if (m_state == BeginDict) {
            m_elements.push_back(ElementInfo(8, 0)); // align only
            m_state = DictKey;
            return;
        }
        break; }

    case EndDict:
    case EndArray: {
        // TODO check that there is at least one type inside the array and that we're at the end of
        //      the current array iteration
        const bool isDict = m_state == BeginDict;
        if (isDict) {
            m_nesting->endParen();
        }
        m_nesting->endArray();
        VALID_IF(!m_aggregateStack.empty());
        aggregateInfo = m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));
        m_aggregateStack.pop_back();
        if (m_zeroLengthArrayNesting) {
            m_zeroLengthArrayNesting--;
        }

        // ### not checking array size here, it may change by a few bytes in the final data stream
        //     due to alignment changes from a different start address
        m_elements.push_back(ElementInfo(1, ElementInfo::ArrayLengthEndMark));
        break; }
    }

    m_state = AnyData;
}

void ArgumentList::WriteCursor::beginArrayOrDict(bool isDict, bool isEmpty)
{
    // can't create an array with contents during type-only iteration
    VALID_IF(!m_zeroLengthArrayNesting || isEmpty);
    if (isEmpty) {
        m_zeroLengthArrayNesting++;
    } else {
        VALID_IF(!m_zeroLengthArrayNesting);
    }
    if (isDict) {
        advanceState(array("a{", strlen("a{")),  BeginDict);
    } else {
        advanceState(array("a", strlen("a")),  BeginArray);
    }
}

void ArgumentList::WriteCursor::beginArray(bool isEmpty)
{
    beginArrayOrDict(false, isEmpty);
}

void ArgumentList::WriteCursor::nextArrayOrDictEntry(bool isDict)
{
    // TODO sanity / syntax checks, data length check too?

    VALID_IF(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    VALID_IF(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));

    if (m_zeroLengthArrayNesting) {
        // allow one iteration to write the types
        VALID_IF(m_signaturePosition == aggregateInfo.arr.containedTypeBegin);
    } else {
        if (m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
            // TODO first iteration, anything to do?
        } else if (isDict) {
            // a dict must have a key and value
            VALID_IF(m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1);
        }
        // array case: we are not at start of contained type's signature, the array is at top of stack
        // -> we *are* at the end of a single complete type inside the array, syntax check passed
        m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
    }

    // TODO need to touch the data? apply alignment?

#undef VALID_IF
}

void ArgumentList::WriteCursor::nextArrayEntry()
{
    nextArrayOrDictEntry(false);
}

void ArgumentList::WriteCursor::endArray()
{
    advanceState(array(), EndArray);
}

void ArgumentList::WriteCursor::beginDict(bool isEmpty)
{
    beginArrayOrDict(true, isEmpty);
}

void ArgumentList::WriteCursor::nextDictEntry()
{
    nextArrayOrDictEntry(true);
}

void ArgumentList::WriteCursor::endDict()
{
    advanceState(array("}", strlen("}")), EndDict);
}

void ArgumentList::WriteCursor::beginStruct()
{
    advanceState(array("(", strlen("(")), BeginStruct);
}

void ArgumentList::WriteCursor::endStruct()
{
    advanceState(array(")", strlen(")")), EndStruct);
}

void ArgumentList::WriteCursor::beginVariant()
{
    advanceState(array("v", strlen("v")), BeginVariant);
}

void ArgumentList::WriteCursor::endVariant()
{
    advanceState(array(), EndVariant);
}

struct ArrayLengthField
{
    uint32 lengthFieldPosition;
    uint32 dataStartPosition;
};

void ArgumentList::WriteCursor::finish()
{
    // what needs to happen here:
    // - check if the message can be closed - basically the aggregate stack must be empty
    // - "pack" or expand the message, depending on how variant support is done
    //   (and resize the data buffer to the minimum required size)
    // - resize the signature to the minimum required size
    assert(m_signaturePosition <= maxSignatureLength); // this should have been caught before
    m_signature.begin[m_signaturePosition] = '\0';
    m_signature.length = m_signaturePosition;

    m_dataPosition = 0;

    byte *buffer = reinterpret_cast<byte *>(malloc(16384)); // TODO dynamic resize
    int bufferPos = 0;
    int count = m_elements.size();
    int variantSignatureIndex = 0;

    std::vector<ArrayLengthField> lengthFieldStack;

    // TODO zero out alignment padding (maybe just zero out the whole allocated buffer beforehand)

    for (int i = 0; i < count; i++) {
        ElementInfo ei = m_elements[i];
        if (ei.size <= ElementInfo::LargestSize) {
            // copy data chunks while applying the proper alignment
            int padStart = bufferPos;
            bufferPos = align(bufferPos, ei.alignment());
            for (; padStart < bufferPos; padStart++) {
                buffer[padStart] = 0; // zero out alignment padding
            }
            m_dataPosition = align(m_dataPosition, ei.alignment());
            memcpy(buffer + bufferPos, m_data.begin + m_dataPosition, ei.size);
            bufferPos += ei.size;
            m_dataPosition += ei.size;
        } else {
            ArrayLengthField al;
            if (ei.size == ElementInfo::ArrayLengthField) {
                // start of an array
                // reserve space for the array length prefix
                bufferPos = align(bufferPos, 4);
                al.lengthFieldPosition = bufferPos;
                bufferPos += 4;
                // array data starts aligned to the first array element
                bufferPos = align(bufferPos, m_elements[i + 1].alignment());
                al.dataStartPosition = bufferPos;
                lengthFieldStack.push_back(al);
            } else if (ei.size == ElementInfo::ArrayLengthEndMark) {
                // end of an array - just put the now known array length in front of the array
                al = lengthFieldStack.back();
                basic::writeUint32(buffer + al.lengthFieldPosition, bufferPos - al.dataStartPosition);
                lengthFieldStack.pop_back();
            } else { // ei.size == ElementInfo::VariantSignature
                // fill in signature (should already include length prefix and trailing null)
                cstring signature = m_variantSignatures[variantSignatureIndex++];
                bufferPos = align(bufferPos, ei.alignment());
                m_dataPosition = align(m_dataPosition, ei.alignment());
                memcpy(buffer + bufferPos, signature.begin, signature.length + 1);
                bufferPos += signature.length + 1;
                free(signature.begin);
            }
        }
    }
    assert(variantSignatureIndex == m_variantSignatures.size());
    assert(lengthFieldStack.empty());
    m_elements.clear();
    m_variantSignatures.clear();

    m_argList->m_signature = m_signature;
    m_argList->m_data = array(buffer, bufferPos);
}

std::vector<ArgumentList::CursorState> ArgumentList::WriteCursor::aggregateStack() const
{
    const int count = m_aggregateStack.size();
    std::vector<CursorState> ret;
    for (int i = 0; i < count; i++) {
        ret.push_back(m_aggregateStack[i].aggregateType);
    }
    return ret;
}

void ArgumentList::WriteCursor::writeByte(byte b)
{
    m_Byte = b;
    advanceState(array("y", strlen("y")), Byte);
}

void ArgumentList::WriteCursor::writeBoolean(bool b)
{
    m_Boolean = b;
    advanceState(array("b", strlen("b")), Boolean);
}

void ArgumentList::WriteCursor::writeInt16(int16 i)
{
    m_Int16 = i;
    advanceState(array("n", strlen("n")), Int16);
}

void ArgumentList::WriteCursor::writeUint16(uint16 i)
{
    m_Uint16 = i;
    advanceState(array("q", strlen("q")), Uint16);
}

void ArgumentList::WriteCursor::writeInt32(int32 i)
{
    m_Int32 = i;
    advanceState(array("i", strlen("i")), Int32);
}

void ArgumentList::WriteCursor::writeUint32(uint32 i)
{
    m_Uint32 = i;
    advanceState(array("u", strlen("u")), Uint32);
}

void ArgumentList::WriteCursor::writeInt64(int64 i)
{
    m_Int64 = i;
    advanceState(array("x", strlen("x")), Int64);
}

void ArgumentList::WriteCursor::writeUint64(uint64 i)
{
    m_Uint64 = i;
    advanceState(array("t", strlen("t")), Uint64);
}

void ArgumentList::WriteCursor::writeDouble(double d)
{
    m_Double = d;
    advanceState(array("d", strlen("d")), Double);
}

void ArgumentList::WriteCursor::writeString(cstring string)
{
    m_String.begin = string.begin;
    m_String.length = string.length;
    advanceState(array("s", strlen("s")), String);
}

void ArgumentList::WriteCursor::writeObjectPath(cstring objectPath)
{
    m_String.begin = objectPath.begin;
    m_String.length = objectPath.length;
    advanceState(array("o", strlen("o")), ObjectPath);
}

void ArgumentList::WriteCursor::writeSignature(cstring signature)
{
    m_String.begin = signature.begin;
    m_String.length = signature.length;
    advanceState(array("g", strlen("g")), Signature);
}

void ArgumentList::WriteCursor::writeUnixFd(uint32 fd)
{
    m_Uint32 = fd;
    advanceState(array("h", strlen("h")), UnixFd);
}
