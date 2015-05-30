/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LGPL.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Alternatively, this file is available under the Mozilla Public License
   Version 1.1.  You may obtain a copy of the License at
   http://www.mozilla.org/MPL/
*/

#include "arguments.h"

#include "../testutil.h"

#include <algorithm>
#include <cstring>
#include <iostream>

// Handy helpers

static void printChunk(chunk a)
{
    std::cout << "Array: ";
    for (int i = 0; i < a.length; i++) {
        std::cout << int(a.begin[i]) << '|';
    }
    std::cout << '\n';
}

static bool chunksEqual(chunk a1, chunk a2)
{
    if (a1.length != a2.length) {
        std::cout << "Different lengths.\n";
        printChunk(a1);
        printChunk(a2);
        return false;
    }
    for (int i = 0; i < a1.length; i++) {
        if (a1.begin[i] != a2.begin[i]) {
            std::cout << "Different content.\n";
            printChunk(a1);
            printChunk(a2);
            return false;
        }
    }
    return true;
}

static bool stringsEqual(cstring s1, cstring s2)
{
    return chunksEqual(chunk(s1.begin, s1.length), chunk(s2.begin, s2.length));
}

static void doRoundtripForReal(const Arguments &original, bool skipNextEntryAtArrayStart,
                               int dataIncrement, bool debugPrint)
{
    Arguments::Reader reader(original);
    Arguments::Writer writer;

    chunk data = original.data();
    chunk shortData;
    bool isDone = false;
    int emptyNesting = 0;
    bool isFirstEntry = false;

    while (!isDone) {
        TEST(writer.state() != Arguments::InvalidData);
        if (debugPrint) {
            std::cout << "Reader state: " << reader.stateString().begin << '\n';
        }

        switch(reader.state()) {
        case Arguments::Finished:
            isDone = true;
            break;
        case Arguments::NeedMoreData: {
            TEST(shortData.length < data.length);
            // reallocate shortData to test that Reader can handle the data moving around - and
            // allocate the new one before destroying the old one to make sure that the pointer differs
            chunk oldData = shortData;
            shortData.length = std::min(shortData.length + dataIncrement, data.length);
            shortData.begin = reinterpret_cast<byte *>(malloc(shortData.length));
            for (int i = 0; i < shortData.length; i++) {
                shortData.begin[i] = data.begin[i];
            }
            // clobber it to provoke errors that only valgrind might find otherwise
            for (int i = 0; i < oldData.length; i++) {
                oldData.begin[i] = '\xff';
            }
            if (oldData.begin) {
                free(oldData.begin);
            }
            reader.replaceData(shortData);
            break; }
        case Arguments::BeginStruct:
            reader.beginStruct();
            writer.beginStruct();
            break;
        case Arguments::EndStruct:
            reader.endStruct();
            writer.endStruct();
            break;
        case Arguments::BeginVariant:
            reader.beginVariant();
            writer.beginVariant();
            break;
        case Arguments::EndVariant:
            reader.endVariant();
            writer.endVariant();
            break;
        case Arguments::BeginArray: {
            isFirstEntry = true;
            bool isEmpty;
            reader.beginArray(&isEmpty);
            writer.beginArray(isEmpty);
            emptyNesting += isEmpty ? 1 : 0;
            break; }
        case Arguments::NextArrayEntry:
            if (reader.nextArrayEntry()) {
                if (isFirstEntry && skipNextEntryAtArrayStart) {
                    isFirstEntry = false;
                } else {
                    writer.nextArrayEntry();
                }
            }
            break;
        case Arguments::EndArray:
            reader.endArray();
            writer.endArray();
            emptyNesting = std::max(emptyNesting - 1, 0);
            break;
        case Arguments::BeginDict: {
            isFirstEntry = true;
            bool isEmpty;
            reader.beginDict(&isEmpty);
            writer.beginDict(isEmpty);
            emptyNesting += isEmpty ? 1 : 0;
            break; }
        case Arguments::NextDictEntry:
            if (reader.nextDictEntry()) {
                if (isFirstEntry && skipNextEntryAtArrayStart) {
                    isFirstEntry = false;
                } else {
                    writer.nextDictEntry();
                }
            }
            break;
        case Arguments::EndDict:
            reader.endDict();
            writer.endDict();
            emptyNesting = std::max(emptyNesting - 1, 0);
            break;
        case Arguments::Byte:
            writer.writeByte(reader.readByte());
            break;
        case Arguments::Boolean:
            writer.writeBoolean(reader.readBoolean());
            break;
        case Arguments::Int16:
            writer.writeInt16(reader.readInt16());
            break;
        case Arguments::Uint16:
            writer.writeUint16(reader.readUint16());
            break;
        case Arguments::Int32:
            writer.writeInt32(reader.readInt32());
            break;
        case Arguments::Uint32:
            writer.writeUint32(reader.readUint32());
            break;
        case Arguments::Int64:
            writer.writeInt64(reader.readInt64());
            break;
        case Arguments::Uint64:
            writer.writeUint64(reader.readUint64());
            break;
        case Arguments::Double:
            writer.writeDouble(reader.readDouble());
            break;
        case Arguments::String: {
            cstring s = reader.readString();
            if (emptyNesting) {
                s = cstring("");
            } else {
                TEST(Arguments::isStringValid(s));
            }
            writer.writeString(s);
            break; }
        case Arguments::ObjectPath: {
            cstring objectPath = reader.readObjectPath();
            if (emptyNesting) {
                objectPath = cstring("/");
            } else {
                TEST(Arguments::isObjectPathValid(objectPath));
            }
            writer.writeObjectPath(objectPath);
            break; }
        case Arguments::Signature: {
            cstring signature = reader.readSignature();
            if (emptyNesting) {
                signature = cstring("");
            } else {
                TEST(Arguments::isSignatureValid(signature));
            }
            writer.writeSignature(signature);
            break; }
        case Arguments::UnixFd:
            writer.writeUnixFd(reader.readUnixFd());
            break;
        default:
            TEST(false);
            break;
        }
    }

    Arguments copy = writer.finish();
    TEST(reader.state() == Arguments::Finished);
    TEST(writer.state() == Arguments::Finished);
    cstring originalSignature = original.signature();
    cstring copySignature = copy.signature();
    if (originalSignature.length) {
        TEST(Arguments::isSignatureValid(copySignature));
        TEST(stringsEqual(originalSignature, copySignature));
    } else {
        TEST(copySignature.length == 0);
    }

    // TODO when it's wired up between Reader and Arguments: chunk originalData = arg.data();
    chunk originalData = original.data();

    chunk copyData = copy.data();
    TEST(originalData.length == copyData.length);
    if (debugPrint && !chunksEqual(originalData, copyData)) {
        printChunk(originalData);
        printChunk(copyData);
    }
    TEST(chunksEqual(originalData, copyData));

    if (shortData.begin) {
        free(shortData.begin);
    }
}

// not returning by value to avoid the move constructor or assignment operator -
// those should have separate tests
static Arguments *shallowCopy(const Arguments &original)
{
    cstring signature = original.signature();
    chunk data = original.data();
    return new Arguments(nullptr, signature, data);
}

static void shallowAssign(Arguments *copy, const Arguments &original)
{
    cstring signature = original.signature();
    chunk data = original.data();
    *copy = Arguments(nullptr, signature, data);
}

static void doRoundtripWithCopyAssignEtc(const Arguments &arg_in, bool skipNextEntryAtArrayStart,
                                         int dataIncrement, bool debugPrint)
{
    {
        // just pass through
        doRoundtripForReal(arg_in, skipNextEntryAtArrayStart, dataIncrement, debugPrint);
    }
    {
        // shallow copy
        Arguments *shallowDuplicate = shallowCopy(arg_in);
        doRoundtripForReal(*shallowDuplicate, skipNextEntryAtArrayStart, dataIncrement, debugPrint);
        delete shallowDuplicate;
    }
    {
        // assignment from shallow copy
        Arguments shallowAssigned;
        shallowAssign(&shallowAssigned, arg_in);
        doRoundtripForReal(shallowAssigned, skipNextEntryAtArrayStart, dataIncrement, debugPrint);
    }
    {
        // deep copy
        Arguments original(arg_in);
        doRoundtripForReal(original, skipNextEntryAtArrayStart, dataIncrement, debugPrint);
    }
    {
        // move construction from shallow copy
        Arguments *shallowDuplicate = shallowCopy(arg_in);
        Arguments shallowMoveConstructed(std::move(*shallowDuplicate));
        doRoundtripForReal(shallowMoveConstructed, skipNextEntryAtArrayStart, dataIncrement, debugPrint);
        delete shallowDuplicate;
    }
    {
        // move assignment (hopefully, may the compiler optimize this to move-construction?) from shallow copy
        Arguments *shallowDuplicate = shallowCopy(arg_in);
        Arguments shallowMoveAssigned;
        shallowMoveAssigned = std::move(*shallowDuplicate);
        doRoundtripForReal(shallowMoveAssigned, skipNextEntryAtArrayStart, dataIncrement, debugPrint);
        delete shallowDuplicate;
    }
    {
        // move construction from deep copy
        Arguments duplicate(arg_in);
        Arguments moveConstructed(std::move(duplicate));
        doRoundtripForReal(moveConstructed, skipNextEntryAtArrayStart, dataIncrement, debugPrint);
    }
    {
        // move assignment (hopefully, may the compiler optimize this to move-construction?) from deep copy
        Arguments duplicate(arg_in);
        Arguments moveAssigned;
        moveAssigned = std::move(duplicate);
        doRoundtripForReal(moveAssigned, skipNextEntryAtArrayStart, dataIncrement, debugPrint);
    }
}

static void doRoundtrip(const Arguments &arg, bool debugPrint = false)
{
    int maxIncrement = arg.data().length;
    for (int i = 1; i <= maxIncrement; i++) {
        doRoundtripWithCopyAssignEtc(arg, false, i, debugPrint);
        doRoundtripWithCopyAssignEtc(arg, true, i, debugPrint);
    }
}



// Tests proper



static void test_stringValidation()
{
    {
        cstring emptyWithNull("");
        cstring emptyWithoutNull;

        TEST(!Arguments::isStringValid(emptyWithoutNull));
        TEST(Arguments::isStringValid(emptyWithNull));

        TEST(!Arguments::isObjectPathValid(emptyWithoutNull));
        TEST(!Arguments::isObjectPathValid(emptyWithNull));

        TEST(Arguments::isSignatureValid(emptyWithNull));
        TEST(!Arguments::isSignatureValid(emptyWithoutNull));
        TEST(!Arguments::isSignatureValid(emptyWithNull, Arguments::VariantSignature));
        TEST(!Arguments::isSignatureValid(emptyWithoutNull, Arguments::VariantSignature));
    }
    {
        cstring trivial("i");
        TEST(Arguments::isSignatureValid(trivial));
        TEST(Arguments::isSignatureValid(trivial, Arguments::VariantSignature));
    }
    {
        cstring list("iqb");
        TEST(Arguments::isSignatureValid(list));
        TEST(!Arguments::isSignatureValid(list, Arguments::VariantSignature));
        cstring list2("aii");
        TEST(Arguments::isSignatureValid(list2));
        TEST(!Arguments::isSignatureValid(list2, Arguments::VariantSignature));
    }
    {
        cstring simpleArray("ai");
        TEST(Arguments::isSignatureValid(simpleArray));
        TEST(Arguments::isSignatureValid(simpleArray, Arguments::VariantSignature));
    }
    {
        cstring messyArray("a(iaia{ia{iv}})");
        TEST(Arguments::isSignatureValid(messyArray));
        TEST(Arguments::isSignatureValid(messyArray, Arguments::VariantSignature));
    }
    {
        cstring dictFail("a{vi}");
        TEST(!Arguments::isSignatureValid(dictFail));
        TEST(!Arguments::isSignatureValid(dictFail, Arguments::VariantSignature));
    }
    {
        cstring emptyStruct("()");
        TEST(!Arguments::isSignatureValid(emptyStruct));
        TEST(!Arguments::isSignatureValid(emptyStruct, Arguments::VariantSignature));
        cstring emptyStruct2("(())");
        TEST(!Arguments::isSignatureValid(emptyStruct2));
        TEST(!Arguments::isSignatureValid(emptyStruct2, Arguments::VariantSignature));
        cstring miniStruct("(t)");
        TEST(Arguments::isSignatureValid(miniStruct));
        TEST(Arguments::isSignatureValid(miniStruct, Arguments::VariantSignature));
        cstring badStruct("((i)");
        TEST(!Arguments::isSignatureValid(badStruct));
        TEST(!Arguments::isSignatureValid(badStruct, Arguments::VariantSignature));
        cstring badStruct2("(i))");
        TEST(!Arguments::isSignatureValid(badStruct2));
        TEST(!Arguments::isSignatureValid(badStruct2, Arguments::VariantSignature));
    }
    {
        cstring nullStr;
        cstring emptyStr("");
        TEST(!Arguments::isObjectPathValid(nullStr));
        TEST(!Arguments::isObjectPathValid(emptyStr));
        TEST(Arguments::isObjectPathValid(cstring("/")));
        TEST(!Arguments::isObjectPathValid(cstring("/abc/")));
        TEST(Arguments::isObjectPathValid(cstring("/abc")));
        TEST(Arguments::isObjectPathValid(cstring("/abc/def")));
        TEST(!Arguments::isObjectPathValid(cstring("/abc&def")));
        TEST(!Arguments::isObjectPathValid(cstring("/abc//def")));
        TEST(Arguments::isObjectPathValid(cstring("/aZ/0123_zAZa9_/_")));
    }
    {
        cstring maxStruct("((((((((((((((((((((((((((((((((i"
                          "))))))))))))))))))))))))))))))))");
        TEST(Arguments::isSignatureValid(maxStruct));
        TEST(Arguments::isSignatureValid(maxStruct, Arguments::VariantSignature));
        cstring struct33("(((((((((((((((((((((((((((((((((i" // too much nesting by one
                         ")))))))))))))))))))))))))))))))))");
        TEST(!Arguments::isSignatureValid(struct33));
        TEST(!Arguments::isSignatureValid(struct33, Arguments::VariantSignature));

        cstring maxArray("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaai");
        TEST(Arguments::isSignatureValid(maxArray));
        TEST(Arguments::isSignatureValid(maxArray, Arguments::VariantSignature));
        cstring array33("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaai");
        TEST(!Arguments::isSignatureValid(array33));
        TEST(!Arguments::isSignatureValid(array33, Arguments::VariantSignature));
    }
}

static void test_nesting()
{
    {
        Arguments::Writer writer;
        for (int i = 0; i < 32; i++) {
            writer.beginArray(false);
            writer.nextArrayEntry();
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.beginArray(false);
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        for (int i = 0; i < 32; i++) {
            writer.beginDict(false);
            writer.nextDictEntry();
            writer.writeInt32(i); // key, next nested dict is value
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.beginStruct();
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        for (int i = 0; i < 32; i++) {
            writer.beginDict(false);
            writer.nextDictEntry();
            writer.writeInt32(i); // key, next nested dict is value
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.beginArray(false);
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        for (int i = 0; i < 64; i++) {
            writer.beginVariant();
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.beginVariant();
        TEST(writer.state() == Arguments::InvalidData);
    }
}

struct LengthPrefixedData
{
    uint32 length;
    byte data[256];
};

static void test_roundtrip()
{
    doRoundtrip(Arguments(nullptr, cstring(""), chunk()));
    {
        byte data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        doRoundtrip(Arguments(nullptr, cstring("i"), chunk(data, 4)));
        doRoundtrip(Arguments(nullptr, cstring("yyyy"), chunk(data, 4)));
        doRoundtrip(Arguments(nullptr, cstring("iy"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("iiy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("nquy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("unqy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("nqy"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("qny"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("yyny"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("qyyy"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("d"), chunk(data, 8)));
        doRoundtrip(Arguments(nullptr, cstring("dy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("x"), chunk(data, 8)));
        doRoundtrip(Arguments(nullptr, cstring("xy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("t"), chunk(data, 8)));
        doRoundtrip(Arguments(nullptr, cstring("ty"), chunk(data, 9)));
    }
    {
        LengthPrefixedData testArray = {0};
        for (int i = 0; i < 64; i++) {
            testArray.data[i] = i;
        }
        byte *testData = reinterpret_cast<byte *>(&testArray);

        testArray.length = 1;
        doRoundtrip(Arguments(nullptr, cstring("ay"), chunk(testData, 5)));
        testArray.length = 4;
        doRoundtrip(Arguments(nullptr, cstring("ai"), chunk(testData, 8)));
        testArray.length = 8;
        doRoundtrip(Arguments(nullptr, cstring("ai"), chunk(testData, 12)));
        testArray.length = 64;
        doRoundtrip(Arguments(nullptr, cstring("ai"), chunk(testData, 68)));
        doRoundtrip(Arguments(nullptr, cstring("an"), chunk(testData, 68)));

        testArray.data[0] = 0; testArray.data[1] = 0; // zero out padding
        testArray.data[2] = 0; testArray.data[3] = 0;
        testArray.length = 56;
        doRoundtrip(Arguments(nullptr, cstring("ad"), chunk(testData, 64)));
    }
    {
        LengthPrefixedData testString;
        for (int i = 0; i < 200; i++) {
            testString.data[i] = 'A' + i % 53; // stay in the 7-bit ASCII range
        }
        testString.data[200] = '\0';
        testString.length = 200;
        byte *testData = reinterpret_cast<byte *>(&testString);
        doRoundtrip(Arguments(nullptr, cstring("s"), chunk(testData, 205)));
    }
    {
        LengthPrefixedData testDict;
        testDict.length = 2;
        testDict.data[0] = 0; testDict.data[1] = 0; // zero padding; dict entries are always 8-aligned.
        testDict.data[2] = 0; testDict.data[3] = 0;

        testDict.data[4] = 23;
        testDict.data[5] = 42;
        byte *testData = reinterpret_cast<byte *>(&testDict);
        doRoundtrip(Arguments(nullptr, cstring("a{yy}"), chunk(testData, 10)));
    }
    {
        byte testData[36] = {
            5, // variant signature length
            '(', 'y', 'g', 'd', ')', '\0', // signature: struct of: byte, signature (easiest because
                                           //   its length prefix is byte order independent), double
            0,      // pad to 8-byte boundary for struct
            23,     // the byte
            6, 'i', 'a', '{', 'i', 'v', '}', '\0', // the signature
            0, 0, 0, 0, 0, 0, 0,    // padding to 24 bytes (next 8-byte boundary)
            1, 2, 3, 4, 5, 6, 7, 8, // the double
            20, 21, 22, 23 // the int (not part of the variant)
        };
        doRoundtrip(Arguments(nullptr, cstring("vi"), chunk(testData, 36)));
    }
}

static void test_writerMisuse()
{
    // Array
    {
        Arguments::Writer writer;
        writer.beginArray(false);
        writer.endArray(); // wrong,  must contain exactly one type
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(true);
        writer.endArray(); // even with no elements it, must contain exactly one type
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(false);
        writer.writeByte(1); // in Writer, calling nextArrayEntry() after beginArray() is optional
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(false);
        writer.nextArrayEntry();    // optional and may not trigger an error
        TEST(writer.state() != Arguments::InvalidData);
        writer.endArray(); // wrong, must contain exactly one type
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(false);
        writer.nextArrayEntry();
        writer.writeByte(1);
        writer.writeByte(2);  // wrong, must contain exactly one type
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(true);
        writer.nextArrayEntry();
        writer.beginVariant();
        writer.endVariant(); // empty variants are okay if and only if inside an empty array
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
    }
    // Dict
    {
        Arguments::Writer writer;
        writer.beginDict(false);
        writer.endDict(); // wrong, must contain exactly two types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(false);
        writer.nextDictEntry();
        writer.writeByte(1);
        writer.endDict(); // wrong, a dict must contain exactly two types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(false);
        writer.writeByte(1); // in Writer, calling nextDictEntry() after beginDict() is optional
        writer.writeByte(2);
        writer.endDict();
        TEST(writer.state() != Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(false);
        writer.nextDictEntry();
        writer.writeByte(1);
        writer.writeByte(2);
        TEST(writer.state() != Arguments::InvalidData);
        writer.writeByte(3); // wrong, a dict contains only exactly two types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(false);
        writer.nextDictEntry();
        writer.beginVariant(); // wrong, key type must be basic
        TEST(writer.state() == Arguments::InvalidData);
    }
    // Variant
    {
        // this and the next are a baseline to make sure that the following test fails for a good reason
        Arguments::Writer writer;
        writer.beginVariant();
        writer.writeByte(1);
        writer.endVariant();
        TEST(writer.state() != Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginVariant();
        writer.endVariant();
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginVariant();
        writer.writeByte(1);
        writer.writeByte(2); // wrong, a variant may contain only one or zero single complete types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginStruct();
        writer.writeByte(1);
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::InvalidData); // can't finish while inside an aggregate
        TEST(arg.signature().length == 0); // should not be written on error
    }
}

void addSomeVariantStuff(Arguments::Writer *writer)
{
    // maybe should have typed the following into hackertyper.com to make it look more "legit" ;)
    static const char *aVeryLongString = "ujfgosuideuvcevfgeoauiyetoraedtmzaubeodtraueonuljfgonuiljofnuilojf"
                                         "0ij948h534ownlyejglunh4owny9hw3v9woni09ulgh4wuvc<l9foehujfigosuij"
                                         "ofgnua0j3409k0ae9nyatrnoadgiaeh0j98hejuohslijolsojiaeojaufhesoujh";
    writer->beginVariant();
        writer->beginVariant();
            writer->beginVariant();
                writer->beginStruct();
                    writer->writeString(cstring("Smoerebroed smoerebroed"));
                    writer->beginStruct();
                        writer->writeString(cstring(aVeryLongString));
                        writer->writeString(cstring("Bork bork bork"));
                        writer->beginVariant();
                            writer->beginStruct();
                                writer->writeString(cstring("Quite nesty"));
                                writer->writeObjectPath(cstring("/path/to/object"));
                                writer->writeUint64(234234234);
                                writer->writeByte(2);
                                writer->writeUint64(234234223434);
                                writer->writeUint16(34);
                            writer->endStruct();
                        writer->endVariant();
                        writer->beginStruct();
                            writer->writeByte(34);
                        writer->endStruct();
                    writer->endStruct();
                    writer->writeString(cstring("Another string"));
                writer->endStruct();
            writer->endVariant();
        writer->endVariant();
    writer->endVariant();
}

static void test_complicated()
{
    Arguments arg;
    {
        Arguments::Writer writer;
        // NeedMoreData-related bugs are less dangerous inside arrays, so we try to provoke one here;
        // the reason for arrays preventing failures is that they have a length prefix which enables
        // and encourages pre-fetching all the array's data before processing *anything* inside the
        // array. therefore no NeedMoreData state happens while really deserializing the array's
        // contents. but we exactly want NeedMoreData while in the middle of deserializing something
        // meaty, specifically variants. see Reader::replaceData().
        addSomeVariantStuff(&writer);

        writer.writeInt64(234234);
        writer.writeByte(115);
        writer.beginVariant();
            writer.beginDict(false);
                writer.writeByte(23);
                writer.beginVariant();
                    writer.writeString(cstring("twenty-three"));
                writer.endVariant();
            writer.nextDictEntry();
                writer.writeByte(83);
                writer.beginVariant();
                writer.writeObjectPath(cstring("/foo/bar/object"));
                writer.endVariant();
            writer.nextDictEntry();
                writer.writeByte(234);
                writer.beginVariant();
                    writer.beginArray(false);
                        writer.writeUint16(234);
                    writer.nextArrayEntry();
                        writer.writeUint16(234);
                    writer.nextArrayEntry();
                        writer.writeUint16(234);
                    writer.endArray();
                writer.endVariant();
            writer.nextDictEntry();
                writer.writeByte(25);
                writer.beginVariant();
                    addSomeVariantStuff(&writer);
                writer.endVariant();
            writer.endDict();
        writer.endVariant();
        writer.writeString("Hello D-Bus!");
        writer.beginArray(false);
            writer.writeDouble(1.567898);
        writer.nextArrayEntry();
            writer.writeDouble(1.523428);
        writer.nextArrayEntry();
            writer.writeDouble(1.621133);
        writer.nextArrayEntry();
            writer.writeDouble(1.982342);
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
    }
    doRoundtrip(arg);
}

static void test_alignment()
{
    Arguments arg;
    {
        Arguments::Writer writer;
        writer.writeByte(123);
        writer.beginArray(false);
        writer.writeByte(64);
        writer.endArray();
        writer.writeByte(123);
        for (int i = 124; i < 150; i++) {
            writer.writeByte(i);
        }

        TEST(writer.state() != Arguments::InvalidData);
        writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
        doRoundtrip(arg);
    }
    {
        Arguments::Writer writer;
        writer.writeByte(123);
        writer.beginStruct();
        writer.writeByte(110);
        writer.endStruct();
        writer.writeByte(200);
        writer.finish();
        doRoundtrip(arg);
    }
}

static void test_arrayOfVariant()
{
    Arguments arg;
    // non-empty array
    {
        Arguments::Writer writer;
        writer.writeByte(123);
        writer.beginArray(false);
        writer.beginVariant();
        writer.writeByte(64);
        writer.endVariant();
        writer.endArray();
        writer.writeByte(123);

        TEST(writer.state() != Arguments::InvalidData);
        writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
        doRoundtrip(arg);
    }
    // empty array
    {
        Arguments::Writer writer;
        writer.writeByte(123);
        writer.beginArray(true);
        writer.beginVariant();
        writer.endVariant();
        writer.endArray();
        writer.writeByte(123);

        TEST(writer.state() != Arguments::InvalidData);
        writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
        doRoundtrip(arg);
    }
}

static void test_realMessage()
{
    Arguments arg;
    // non-empty array
    {
        Arguments::Writer writer;

        writer.writeString(cstring("message"));
        writer.writeString(cstring("konversation"));

        writer.beginArray(true);
        writer.beginVariant();
        writer.endVariant();
        writer.endArray();

        writer.writeString(cstring(""));
        writer.writeString(cstring("&lt;fredrikh&gt; he's never on irc"));

        writer.beginArray(true);
        writer.writeByte(123); // may not show up in the output
        writer.endArray();

        writer.beginArray(true);
        writer.writeString(cstring("dummy, I may not show up in the output!"));
        writer.endArray();

        writer.writeInt32(-1);
        writer.writeInt64(46137372);

        TEST(writer.state() != Arguments::InvalidData);
        writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
    }
    doRoundtrip(arg);
}

static void writeValue(Arguments::Writer *writer, int typeIndex, const void *value)
{
    switch (typeIndex) {
    case 0:
        break;
    case 1:
        writer->writeByte(*static_cast<const byte *>(value)); break;
    case 2:
        writer->writeUint16(*static_cast<const uint16 *>(value)); break;
    case 3:
        writer->writeUint32(*static_cast<const uint32 *>(value)); break;
    case 4:
        writer->writeUint64(*static_cast<const uint64 *>(value)); break;
    default:
        TEST(false);
    }
}

static bool checkValue(Arguments::Reader *reader, int typeIndex, const void *expected)
{
    switch (typeIndex) {
    case 0:
        return true;
    case 1:
        return reader->readByte() == *static_cast<const byte *>(expected);
    case 2:
        return reader->readUint16() == *static_cast<const uint16 *>(expected);
    case 3:
        return reader->readUint32() == *static_cast<const uint32 *>(expected);
    case 4:
        return reader->readUint64() == *static_cast<const uint64 *>(expected);
    default:
        TEST(false);
    }
    return false;
}

void test_primitiveArray()
{
    // TODO also test some error cases

    static const uint32 testDataSize = 16384;
    byte testData[testDataSize];
    for (int i = 0; i < testDataSize; i++) {
        testData[i] = i & 0xff;
    }

    for (int i = 0; i < 4; i++) {

        const bool writeAsPrimitive = i & 0x1;
        const bool readAsPrimitive = i & 0x2;

        static const uint32 arrayTypesCount = 5;
        // those types must be compatible with writeValue() and readValue()
        static Arguments::IoState arrayTypes[arrayTypesCount] = {
            Arguments::InvalidData,
            Arguments::Byte,
            Arguments::Uint16,
            Arguments::Uint32,
            Arguments::Uint64
        };

        for (int otherType = 0; otherType < arrayTypesCount; otherType++) {

            // an array with no type in it is ill-formed, so we start with 1 (Byte)
            for (int typeInArray = 1; typeInArray < arrayTypesCount; typeInArray++) {

                static const uint32 arraySizesCount = 12;
                static const uint32 arraySizes[arraySizesCount] = {
                    0,
                    1,
                    2,
                    3,
                    4,
                    7,
                    8,
                    9,
                    511,
                    512,
                    513,
                    2048 // dataSize / sizeof(uint64) == 2048
                };

                for (int k = 0; k < arraySizesCount; k++) {

                    static const uint64_t otherValue = ~0ll;
                    const uint32 arraySize = arraySizes[k];
                    const uint32 dataSize = arraySize << (typeInArray - 1);
                    TEST(dataSize <= testDataSize);

                    Arguments arg;
                    {
                        Arguments::Writer writer;

                        // write something before the array to test different starting position alignments
                        writeValue(&writer, otherType, &otherValue);

                        if (writeAsPrimitive) {
                            writer.writePrimitiveArray(arrayTypes[typeInArray], chunk(testData, dataSize));
                        } else {
                            writer.beginArray(!arraySize);
                            byte *testDataPtr = testData;
                            if (arraySize) {
                                for (int m = 0; m < arraySize; m++) {
                                    writer.nextArrayEntry();
                                    writeValue(&writer, typeInArray, testDataPtr);
                                    testDataPtr += 1 << (typeInArray - 1);
                                }
                            } else {
                                writeValue(&writer, typeInArray, testDataPtr);
                            }
                            writer.endArray();
                        }

                        TEST(writer.state() != Arguments::InvalidData);
                        // TEST(writer.state() == Arguments::AnyData);
                        // TODO do we handle AnyData consistently, and do we really need it anyway?
                        writeValue(&writer, otherType, &otherValue);
                        TEST(writer.state() != Arguments::InvalidData);
                        arg = writer.finish();
                        TEST(writer.state() == Arguments::Finished);
                    }

                    {
                        Arguments::Reader reader(arg);

                        TEST(checkValue(&reader, otherType, &otherValue));

                        if (readAsPrimitive) {
                            TEST(reader.state() == Arguments::BeginArray);
                            std::pair<Arguments::IoState, chunk> ret = reader.readPrimitiveArray();
                            TEST(ret.first == arrayTypes[typeInArray]);
                            TEST(chunksEqual(chunk(testData, dataSize), ret.second));
                        } else {
                            TEST(reader.state() == Arguments::BeginArray);
                            bool isEmpty = false;
                            reader.beginArray(&isEmpty);
                            TEST(isEmpty == (arraySize == 0));
                            TEST(reader.state() != Arguments::InvalidData);
                            byte *testDataPtr = testData;

                            if (arraySize) {
                                for (int m = 0; m < arraySize; m++) {
                                    TEST(reader.state() != Arguments::InvalidData);
                                    TEST(reader.nextArrayEntry());
                                    TEST(checkValue(&reader, typeInArray, testDataPtr));
                                    TEST(reader.state() != Arguments::InvalidData);
                                    testDataPtr += 1 << (typeInArray - 1);
                                }
                            } else {
                                TEST(reader.nextArrayEntry());
                                TEST(reader.state() == arrayTypes[typeInArray]);
                                // next: dummy read, necessary to move forward; value is ignored
                                checkValue(&reader, typeInArray, testDataPtr);
                                TEST(reader.state() != Arguments::InvalidData);
                            }

                            TEST(!reader.nextArrayEntry());
                            TEST(reader.state() != Arguments::InvalidData);
                            reader.endArray();
                            TEST(reader.state() != Arguments::InvalidData);
                        }

                        TEST(reader.state() != Arguments::InvalidData);
                        TEST(checkValue(&reader, otherType, &otherValue));
                        TEST(reader.state() == Arguments::Finished);
                    }
                }
            }
        }
    }
}

// TODO: test where we compare data and signature lengths of all combinations of zero/nonzero array
//       length and long/short type signature, to make sure that the signature is written but not
//       any data if the array is zero-length.

// TODO test empty dicts, too

int main(int argc, char *argv[])
{
    test_stringValidation();
    test_nesting();
    test_roundtrip();
    test_writerMisuse();
    // TODO test_maxArrayLength
    // TODO test_maxFullLength
    // TODO test arrays where array length does not align with end of an element
    //      (corruption of serialized data)
    test_complicated();
    test_alignment();
    test_arrayOfVariant();
    test_realMessage();
    test_primitiveArray();
    // TODO many more misuse tests for Writer and maybe some for Reader
    std::cout << "Passed!\n";
}
