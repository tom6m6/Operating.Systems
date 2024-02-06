clang test.c -o test

./test W R R 
echo GET RAMrandWRITE
./test R R R 
echo GET RAMrandREAD
rm /root/myram/*.txt


./test W R D
echo GET DISKrandWRITE
./test R R D
echo GET DISKrandREAD
rm /usr/*.txt

./test W O R 
echo GET RAMorderWRITE
./test R O R
echo GET RAMorderREAD
rm /root/myram/*.txt

./test W O D
echo GET DISKorderWRITE
./test R O D 
echo GET DISKorderREAD
rm /usr/*.txt