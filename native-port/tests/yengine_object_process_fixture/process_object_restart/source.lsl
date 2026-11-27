YOption arrays;
object boxed = (object)7;
object alias = boxed;
list values = [1];
object list_alias = (object)values;
string cached = (string)42;
string cache_alias = cached;
string left = llGetSubString("xsame", 1, -1);
string right = llGetSubString("ysame", 1, -1);
integer before_hash;
integer after_hash;
integer repeat_hash;
integer before_box_equal;
integer after_box_equal;
integer strings_equal;
integer cache_equal;
integer list_equal;
integer entries;
integer complete;
default
{
    state_entry()
    {
        entries++;
        before_box_equal = (boxed == alias);
        before_hash = xmrHashCode((object)values);
        llSetObjectDesc("object sleeping");
        llSleep(3.0);
        after_box_equal = (boxed == alias);
        strings_equal = ((object)left == (object)right);
        cache_equal = ((object)cached == (object)(string)42);
        list_equal = ((object)values == list_alias);
        after_hash = xmrHashCode((object)values);
        repeat_hash = xmrHashCode(list_alias);
        complete++;
        llSetObjectDesc("object complete:" + (string)after_box_equal + ":" + (string)strings_equal + ":" +
            (string)cache_equal + ":" + (string)list_equal + ":" + (string)entries + ":" + (string)complete);
    }
}
