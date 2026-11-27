YOption allowall;
integer[] input; integer[] alias; integer[] restored; object wrapped;
integer same_alias; integer same_cast; string type_name; string text; integer after;
default { state_entry() {
    input = new integer[](2); input[0] = 31; alias = input; wrapped = input;
    restored = (integer[])wrapped; same_alias = (input == alias); same_cast = (input == restored);
    type_name = xmrTypeName(wrapped); text = (string)wrapped;
    llSleep(0.1); after = (input == alias) && (restored == input) && (input[0] == 31);
} }
