#!python.exe
#coding:utf-8
import sys,os
import urllib
query_string = os.getenv('QUERY_STRING')
print ('Content-type:text/html\n')

if query_string=="trial" :
    print ('<html>')
    print ('<head>')
    print ('<title>try user</title>') 
    print ('</head>') 
    print ('<body>' )
    print ('<h2> Trial Account: </h2>')
    print ('<ul>')
    print ('<li>'+"user=admin" + '</li>')
    print ('<li>'+"passwd=123456"+ '</li>')
    print ('</ul>')
    print ('</body>')
    print ('</html>')

else :
    print ("Content-type:text/html\n")
    print ('no found')