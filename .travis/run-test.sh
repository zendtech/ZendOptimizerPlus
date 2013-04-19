#!/bin/sh
PHP_VERSION=`php-config --vernum`
php-config --configure-options | grep "enable-debug" >/dev/null
php_debug=$?
php_zts=`php -r "echo PHP_ZTS;"`
TEST_DIR="`pwd`/tests"

export TEST_PHP_EXECUTABLE=`which php`

if [ $PHP_VERSION -lt 50300 ] 
then
	if [ $php_debug -eq 0 ]
	then
		if [ $php_zts -eq 1 ]
		then
			export TEST_PHP_ARGS="-d zend_extension_debug_ts=`pwd`/modules/opcache.so"
		else 
			export TEST_PHP_ARGS="-d zend_extension_debug=`pwd`/modules/opcache.so"
		fi
	else
		export TEST_PHP_ARGS="-d zend_extension=`pwd`/modules/opcache.so"
	fi
else
	export TEST_PHP_ARGS="-d zend_extension=`pwd`/modules/opcache.so"
fi

$TEST_PHP_EXECUTABLE run-tests.php -n tests/*.phpt $TEST_DIR

for file in `find $TEST_DIR -name "*.diff" 2>/dev/null`
do
	grep "\-\-XFAIL--" ${file/%".diff"/".phpt"} >/dev/null 2>&1
	if [ $? -gt 0 ]
	then
		FAILS[${#FAILS[@]}]="$file"
	fi
done

if [ ${#FAILS[@]} -gt 0 ]
then
	for fail in "${FAILS[@]}"
	do
		sh -xc "cat $fail"
	done
	exit 1
else
	exit 0
fi
