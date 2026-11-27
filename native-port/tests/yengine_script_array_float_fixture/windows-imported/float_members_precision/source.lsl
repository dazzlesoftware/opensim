YOption allowall;
float[] input; object raw; float default_cell; float observed; float after;
integer length; integer dimension; integer invalid_dimension; integer flat_index; string type_name; string raw_type;
default { state_entry() {
 input=new float[](3); default_cell=input[0]; input.Set(1,1.2345678901234567); input[2]=-2.5;
 raw=input.obj; length=input.Length; dimension=input.Length(0); invalid_dimension=input.Length(-1);
 flat_index=input.Index(2); observed=input.Get(1)+input[2]; type_name=xmrTypeName((object)input); raw_type=xmrTypeName(raw);
 llSleep(0.1); after=input.Get(1);
} }
