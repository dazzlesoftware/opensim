YOption chars;
integer input = 65536;
integer negative = -1;
char initial = (char)65535;
char narrowed;
char returned;
integer parameter;
integer wide_before;
integer negative_before;
integer wide_after;
integer negative_after;
integer calls;
integer entries;
integer done;
char echo(char value) {
    char wide = (char)input;
    char minus = (char)negative;
    ++calls;
    parameter = (integer)value;
    narrowed = (char)input;
    wide_before = (integer)wide;
    negative_before = (integer)minus;
    llSetObjectDesc("character sleeping");
    llOwnerSay("character sleeping");
    llSleep(3.0);
    wide_after = (integer)wide;
    negative_after = (integer)minus;
    return (char)negative;
}
default {
    state_entry() {
        ++entries;
        returned = echo((char)input);
        done = 1;
        llSetObjectDesc("character completed");
    }
}