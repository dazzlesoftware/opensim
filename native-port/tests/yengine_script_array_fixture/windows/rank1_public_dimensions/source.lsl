YOption allowall;
integer[] input; integer length; integer dimension; integer outside_dimension;
integer flat_index; integer after; integer out_of_bounds;
default { state_entry() {
    input = new integer[](3); input[2] = 11; input.len0 = 1;
    length = input.Length; dimension = input.Length(0);
    outside_dimension = input[2]; flat_index = input.Index(2);
    llSleep(0.1); after = input.Get(2); out_of_bounds = input[3];
} }
