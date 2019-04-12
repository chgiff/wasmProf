int f1(int a, int b){
  return f2(a+b);
}

int f2(int a){
  if(a > 10) return a;
  return f1(a,a+3);
}


int main()
{
  return f1(1,0);
}