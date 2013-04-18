#!/bin/sh
PHP_VERSION=`php-config --version`

export TEST_PHP_EXECUTABLE=`which php`

if [ "$PHP_VERSION" = "`echo -e "$PHP_VERSION\n5.3.0" | sort -n | head -n1`" ] 
then
	export TEST_PHP_ARGS="-n -d zend_extension_debug=`pwd`/modules/opcache.so"
else
	export TEST_PHP_ARGS="-n -d zend_extension=`pwd`/modules/opcache.so"
fi

$TEST_PHP_EXECUTABLE run-tests.php tests/*.phpt

ls tests/*.diff 1>/dev/null 2>&1
ret_code=$?

tests/*.mem 1>/dev/null 2>&1
ret_code=$(($ret_code & $?))

if [ $ret_code -eq 0 ] 
then
	find tests -name "*.diff" -or -name "*.mem" | xargs -i sh -xc "cat {}"
	exit 1
else
	exit 0
fi
