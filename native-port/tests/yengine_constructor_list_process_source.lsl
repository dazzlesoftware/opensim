integer calls;
integer entries;
list early = pause(later);
list later;

list pause(list value) {
    ++calls;
    llSetObjectDesc("list constructor sleeping");
    llOwnerSay("list constructor sleeping");
    llSleep(3.0);
    return value;
}

default {
    state_entry() {
        ++entries;
        llSetObjectDesc("list ctor complete:" + (string)(early == []) + ":" +
                        (string)llGetListLength(later) + ":" + (string)calls + ":" + (string)entries);
    }
}
