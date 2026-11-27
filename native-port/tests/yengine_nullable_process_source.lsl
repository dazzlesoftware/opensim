string global;
string result;
list items;
integer calls;
integer entries;
integer done;

string pause(string value) {
    string local = value;
    ++calls;
    llSetObjectDesc("nullable sleeping");
    llOwnerSay("nullable sleeping");
    llSleep(3.0);
    return local;
}

default {
    state_entry() {
        ++entries;
        global = llComputeHash("payload", "unsupported");
        items = [global];
        result = pause(global);
        done = 1;
        llSetObjectDesc("nullable completed:" + (string)(global == "") + ":" +
                        (string)(result == "") + ":" + (string)calls + ":" + (string)entries);
    }
}
