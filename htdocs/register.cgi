#!python.exe
#coding:utf-8
import sys,os
import urllib
length = os.getenv('CONTENT_LENGTH')

if length:
    postdata = sys.stdin.read(int(length))
    print ("Content-type:text/html\n")
    print ('<html>')
    print ('<head>')
    print ('<title>Registered user information</title>') 
    print ('</head>') 
    print ('<body>' )
    print ('<h2> Registered user information: </h2>')
    print ('<ul>')
    for data in postdata.split('&'):
    	print  ('<li>'+data+'</li>')
    print ('</ul>')
    print ('</body>')
    print ('</html>')
    
else:
    print ("Content-type:text/html\n")
    print ('no found')


