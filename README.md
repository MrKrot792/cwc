# What is `cwc`?
`cwc` (cool website compiler) - is a dead simple CLI tool to compile
site templates to actual projects written in C. Everything is managed
through source code, except something essential like site directory
and output directory.

I admit it, it's full of bugs and segfaults, but I wrote it overnight
and I will fix it later.

# How to build?
`cwc` need meson, ninja and gcc to be built. Then you can build it
like this:
```sh
meson setup build
ninja -C build
```
or like this to release-build:
```sh
meson setup build -Dreleasetype=release
ninja -C build
```
and then (after either one of these) install it:
```sh
ninja -C build install
```
it'll install it to `/usr/bin/local`.

# How to use?
## General overview
`cwc` requires an appropriate file structure. It operates on file
directories recursively. It's input directory must contain a
`template.html` file in it's root, and other `.html` files in the same
directory.

It will also not process (just copy) any files in the subdirectory
called `assets`, which can be changed throught the source code.

## Cli use
```sh
cwc <input_directory> <output_directory>
```

Both `<input_directory` and `<output_directory>` are optional, and
will be defaulted to (in order): `site` and `build-site`.

## How does it work?
It takes `template.html`, and pastes appropriate labels from different
files inside of it, replacing the original file.

Example: Suppose our `template.html` is a file with the following
content:
```html
<div>
  <ol>
	<li>@%l1@%</li>
	<li>@%l1@%</li>
	<li>@%l2@%</li>
	<li>@%l3@%</li>
  </ol>
</div>
```
And our regular file is a `homepage.html`. It's content:
```html
@%l1@%
<p>
  Hello, world!
</p>

@%l2@%
hi hi hi!!!

@%l3@%
<h1>l3 i guess</h1>
```

After we run the cli, assuming that `template.html` and
`homepage.html` are in the same, empty directory called `site`, `cwc`
will produce a directory called `build-site` with a single file in it:
`homepage.html`. That file will contain the expanded template code:
```html
<div>
  <ol>
    <li>
<p>
  Hello, world!
</p>

</li>
    <li>
<p>
  Hello, world!
</p>

</li>
    <li>
hi hi hi!!!

</li>
    <li>
<h1>l3 i guess</h1></li>
  </ol>
</div>
```
(this is the raw output of the program, a little bit tidier version's
here)
```html
<div>
  <ol>
    <li>
      <p>
	    Hello, world!
      </p>
    </li>
    <li>
      <p>
	    Hello, world!
      </p>
    </li>
    <li>
      hi hi hi!!!
    </li>
    <li>
      <h1>l3 i guess</h1>
	</li>
  </ol>
</div>
```

Yeah that's it
