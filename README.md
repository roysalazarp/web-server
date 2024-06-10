<div align="center">
    <h1>Renaissance</h1>
    <h5> Application backend written in ANSI C </h3>
    <img alt="renaissance c logo" height="250" src="./c-renaissance.png" />
</div>

<div>
    <h2>⇁ TOC</h2>
</div>

- [⇁ The Problem](#-the-problem)
- [⇁ The Solution](#-the-solution)
- [⇁ Installation](#-installation)
  - [Github codespaces](#github-codespaces)
  - [Local setup](#local-setup)
- [⇁ Running the project](#-running-the-project)
  - [VS Code](#vs-code)
  - [Valgrind](#valgrind)
  - [Executable binary](#executable-binary)
- [⇁ Code style](#-code-style)
- [⇁ Features](#-features)
- [⇁ Feedback](#-feedback)
- [⇁ Contribution](#-contribution)
- [⇁ Social](#-social)
- [⇁ Acknowledgements](#-acknowledgements)

## ⇁ The Problem

It turns out that piling code on top of code doesn't work for creating simple, reliable, secure, and performant applications.

## ⇁ The Solution

Remove the millions of lines of code that "make your life easier" and instead give the hardware only the necessary instructions to perform the task at hand.

## ⇁ Installation

This project is designed to run on Linux and depends on features like signal handling, threading, network sockets, and others.

### Github codespaces

The `devcontainer.json` file will configure most of your environment, but you'll still need to install Valgrind manually.

### Local setup

Before running the project locally, make sure you have these tools installed:

-   GCC
-   GDB
-   Curl (optipnal)
-   Valgrind (optipnal)
-   Git (optional)

## ⇁ Running the project

### VS Code

The project already contains the necessary setup to run with the VS Code debugger.

### Valgrind

```sh
$ ./build.sh
$ valgrind ./build/main
```

To exit the program, press CTRL+C (`^C`) at the terminal window. The server will terminate, and you'll then see the Valgrind report.

### Executable binary

```sh
$ ./build.sh
$ ./build/main
```

To exit the program, press CTRL+C (`^C`) at the terminal window. The server will terminate.

## ⇁ Code style

-   Functions return errors as values. During server setup, a panic is triggered if an error occurs. During request handling, error data is sent to the client if possible.

-   Functions receive an uninitialized buffer, intended to store the return data, as their first argument and are responsible for allocating memory for this buffer. If the function fails, it should also free the buffer. If the function succeeds, it becomes the calling function's responsibility to free the buffer after use.

-   The use of `goto` when errors occur helps reduce redundant cleanup code.

```c
Error my_function(SomeStruct *buffer) {
    Error error = {0};

    /** Allocate memory */
    buffer->foo = (char *)malloc(/* size */);
    if (buffer->foo == NULL) {
        sprintf(error.message, "Failed to allocate memory for buffer->foo");
        error.panic = DEBUG;
        return error;
    }

    char some_string[] = "hello world!";
    if (memcpy(buffer->foo, some_string, strlen(some_string)) == NULL) {
        sprintf(error.message, "Failed to copy data to buffer->foo");
        error.panic = DEBUG;
        free(buffer->foo); /** Free allocated memory */
        return error;
    }

    return error;
}

Error parent_function() {
    Error error = {0};

    SomeStruct buffer = {0};

    if ((error = my_function(&buffer)).panic) {
        goto parent_function_cleanup;
    }

    /** ... */

parent_function_cleanup:
    /** ... */

    some_struct_free(&buffer); /** Free allocated memory */

    return error;
}
```

## ⇁ Features

Coming soon...

## ⇁ Feedback

I'd love to hear from you! Please share your feedback by opening an issue.

## ⇁ Contribution

While this project is publicly available, it is not open for contributions. However, you are encouraged to fork the project for your own use.

## ⇁ Social

-   [LinkedIn](https://www.linkedin.com/in/roy-salazar-a93b0b192/)
-   [Twitter](https://x.com/roysalazardev)

## ⇁ Acknowledgements

I extend my appreciation to Casey Muratori and Jonathan Blow for inspiring me to strive for high-performance, quality software.
