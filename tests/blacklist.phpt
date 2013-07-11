--TEST--
Blacklist (with glob, quote and comments)
--INI--
opcache.enable=1
opcache.enable_cli=1
opcache.blacklist_filename={PWD}/opcache-*.blacklist
--SKIPIF--
<?php require_once('skipif.inc'); ?>
--FILE--
<?php
$conf = opcache_get_configuration();
$conf = $conf['blacklist'];
$conf[3] = preg_replace("!^\\Q".getcwd()."\\E!", "CWD", $conf[3]); 
$conf[4] = preg_replace("!^\\Q".getcwd()."\\E!", "CWD", $conf[4]); 
print_r($conf);
?>
--EXPECT--
Array
(
    [0] => /path/to/foo
    [1] => /path/to/foo2
    [2] => /path/to/bar
    [3] => CWD/nopath.php
    [4] => CWD/current.php
    [5] => /tmp/path/?nocache.inc
    [6] => /tmp/path/*/somedir
)
