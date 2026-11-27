YOption chars;
string original;
list boxed;
string result;
integer unit_before;
integer unit_after;
integer calls;
integer entries;
integer done;
string echo(string value) {
    string local = value;
    ++calls;
    unit_before = (integer)local[0];
    llSetObjectDesc("utf16 sleeping:" + (string)unit_before);
    llOwnerSay("utf16 sleeping");
    llSleep(3.0);
    unit_after = (integer)local[0];
    return local;
}
default {
    state_entry() {
        ++entries;
        original = llGetSubString("😀", 0, 0);
        boxed = [original];
        result = echo(original);
        done = 1;
        llSetObjectDesc("utf16 completed:" + (string)unit_after + ":" + (string)calls + ":" + (string)entries);
    }
}