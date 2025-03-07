
<!--
$ [ -x "$bin/tarlike" ] && echo "ok"        # byexample: +fail-fast
ok

$ mkdir -p tmp/
$ rm -f tmp/myarchive.xoz
-->

# Tarlike

`tarlike` is a toy program that emulates the Unix/Linux classic `tar`
achiver and it is the perfect excuse to play with the `xoz` library.

Here are what we can do:

```shell
$ ./$bin/tarlike                    # byexample: +norm-ws -capture +fail-fast
Missing/Bad arguments
Usage:
    add files:      tarlike <file.xoz> a <file name> [<file name>...]
    delete files:   tarlike <file.xoz> d <file id> [<file id>...]
    extract files:  tarlike <file.xoz> x <file id> [<file id>...]
    rename a file:  tarlike <file.xoz> r <file id> <new file name>
    list files:     tarlike <file.xoz> l
    show stats:     tarlike <file.xoz> s
```

Listing a non-existing file is not an error, it is just an empty
archive:

```shell
$ ./$bin/tarlike tmp/myarchive.xoz  l
```

But that's too boring. Let's add some files to the archive:

```shell
$ ./$bin/tarlike tmp/myarchive.xoz  a  test/tarlike-files/*
[ID 1] File test/tarlike-files/ada added.
[ID 2] File test/tarlike-files/basic added.
[ID 3] File test/tarlike-files/csharp added.
```

```shell
$ ./$bin/tarlike tmp/myarchive.xoz  l
[ID 1] File ada
[ID 2] File basic
[ID 3] File csharp
```

We can change the name of the files:

```shell
$ ./$bin/tarlike tmp/myarchive.xoz  r 3 "c#"
[ID 3] File c# renamed.
```

Or we can delete them:

```shell
$ ./$bin/tarlike tmp/myarchive.xoz  d 2
[ID 2] File basic removed.
```

And of course, we can extract them!

```shell
$ ./$bin/tarlike tmp/myarchive.xoz  x 1 3
[ID 1] File ada extracted
[ID 3] File c# extracted
```

See? they were added and later extracted without any corruption.

```shell
$ diff ada test/tarlike-files/ada
$ diff 'c#' test/tarlike-files/csharp
```


<!--
$ rm -f tmp/myarchive.xoz   # byexample: -skip +pass
$ rm -f 'ada' 'c#'          # byexample: -skip +pass
-->
