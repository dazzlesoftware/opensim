YOption allowall;
integer[] input;
integer default_cell; integer length; integer dimension; integer invalid_dimension;
integer flat_index; integer observed; integer after;
default { state_entry() {
    input = new integer[](3);
    default_cell = input[0]; input[1] = 7; input.Set(2,9);
    length = input.Length; dimension = input.Length(0); invalid_dimension = input.Length(-1);
    flat_index = input.Index(2); observed = input.Get(1) + input[2];
    llSleep(0.1); after = input[1] + input.Get(2);
} }
