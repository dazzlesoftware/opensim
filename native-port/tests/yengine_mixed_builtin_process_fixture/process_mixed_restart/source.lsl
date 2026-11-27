integer length = llStringLength([1,2]);
string slice = llGetSubString(12345,1,3);
integer found = llListFindList(1,1);
integer order;
string joined;
string current = "a";
string deferred;
string argument;
string result;
integer calls;
integer entries;
integer done;
string left() { order = order*10+1; return "l"; }
string right() { order = order*10+2; return "r"; }
string mutate() { current = "b"; return "c"; }
string pause(string value) {
    ++calls;
    argument = value;
    llSetObjectDesc("mixed sleeping");
    llOwnerSay("mixed sleeping");
    llSleep(3.0);
    return value;
}
default {
    state_entry() {
        ++entries;
        joined = left()+right();
        deferred = current+mutate();
        result = pause((7+"x")+llDumpList2String(1.25,"|"));
        done = 1;
        llSetObjectDesc("mixed completed");
    }
}