int add(int a, int b){
  return a + b;
}

int mult(int a, int b){
  return a + a;
}

typedef int (*Math)(int a, int b);

int main()
{
  Math fun;
  int num = add(2,6);
  if(num > 4) fun = &add;
  else fun = &mult;
  
  return fun(2,5);
}