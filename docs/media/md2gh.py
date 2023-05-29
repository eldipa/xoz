import sys, re

# Hack: convert "*foo* into "$color...foo..$"
# to make colored text in Github Wiki
emph = re.compile(r'(?<!\*)\*([^ *].*?)\*')

# Same for snippets
code = re.compile(r'(?<!`)`([^ `].*?)`')

def text_wrap(s):
    pieces = [r'\textsf{' + w + r'}' for w in s.split('_')]
    return r'\textunderscore '.join(pieces)

def emph_repl(m):
    s = text_wrap(m.group(1))
    return r'$\color{brown}{' + s + r'}$'

def code_repl(m):
    s = text_wrap(m.group(1))
    return r'$\color{teal}{' + s + r'}$'

if __name__ == '__main__':
    fname = sys.argv[1]
    with open(fname, 'rt') as md:
        for line in md:
            line = emph.sub(emph_repl, line)
            line = code.sub(code_repl, line)

            print(line, end='')

