YOption allowall;
float[] input; float[] alias; object raw; integer length; float outside_dimension; float after;
default { state_entry() {
 input=new float[](3); input[2]=12.125; alias=input; raw=input.obj; input.len0=1;
 length=input.Length; outside_dimension=alias[2]; xmrFixedArraySetF(raw,2,13.375);
 llSleep(0.1); after=input.Get(2);
} }
