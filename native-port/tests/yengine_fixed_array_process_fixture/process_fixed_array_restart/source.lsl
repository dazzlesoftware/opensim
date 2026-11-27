YOption arrays;
YOption chars;
object ints;
object int_alias;
object chars;
object floats;
object objects;
object object_alias;
object typename;
integer before_hash;
integer after_hash;
integer repeat_hash;
integer contents;
integer aliases;
integer type_cache_equal;
integer entries;
integer complete;
default
{
    state_entry()
    {
        entries++;
        ints = xmrFixedArrayAllocI(2);
        xmrFixedArraySetI(ints, 0, 19);
        xmrFixedArraySetI(ints, 1, -7);
        int_alias = ints;
        chars = xmrFixedArrayAllocC(2);
        xmrFixedArraySetC(chars, 0, 'A');
        xmrFixedArraySetC(chars, 1, (char)955);
        floats = xmrFixedArrayAllocF(1);
        xmrFixedArraySetF(floats, 0, xmrString2Float("1.234567891"));
        objects = xmrFixedArrayAllocO(3);
        xmrFixedArraySetO(objects, 0, ints);
        xmrFixedArraySetO(objects, 1, chars);
        xmrFixedArraySetO(objects, 2, "payload");
        object_alias = objects;
        typename = xmrTypeName(objects);
        before_hash = xmrHashCode(objects);
        llSetObjectDesc("array sleeping");
        llSleep(3.0);
        contents = (xmrFixedArrayGetI(ints, 0) == 19 && xmrFixedArrayGetI(int_alias, 1) == -7 &&
            (integer)xmrFixedArrayGetC(chars, 0) == 65 && (integer)xmrFixedArrayGetC(chars, 1) == 955 &&
            xmrFixedArrayGetF(floats, 0) == xmrString2Float("1.234567891") &&
            (string)xmrFixedArrayGetO(objects, 2) == "payload");
        aliases = (ints == int_alias && objects == object_alias &&
            xmrFixedArrayGetO(objects, 0) == ints && xmrFixedArrayGetO(objects, 1) == chars);
        type_cache_equal = (typename == (object)xmrTypeName(objects));
        after_hash = xmrHashCode(objects);
        repeat_hash = xmrHashCode(object_alias);
        complete++;
        llSetObjectDesc("array complete:" + (string)contents + ":" + (string)aliases + ":" +
            (string)type_cache_equal + ":" + (string)entries + ":" + (string)complete);
    }
}
