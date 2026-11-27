string gi = 7;
string gl = [1,2.5];
string gf = xmrString2Float("0.0000005");
string gv = <1,2,3>;
string gr = <0,0,0,1>;
key gk = [NULL_KEY];
string result;
string argument;
integer calls;
integer entries;
integer done;
string echo(string value) {
    ++calls;
    argument = value;
    llSetObjectDesc("text sleeping");
    llOwnerSay("text sleeping");
    llSleep(3.0);
    return [value,xmrString2Float("0.0000005")];
}
default {
    state_entry() {
        ++entries;
        result = echo([1,2.5]);
        done = 1;
        llSetObjectDesc("text completed");
    }
}