import sys, re

# Hack: convert "*foo* into "`foo`"
# to make colored text in Github Wiki
emph = re.compile(r'(?<!\*)\*([^ *].*?)\*')

def text_wrap(s):
    return s

def emph_repl(m):
    s = text_wrap(m.group(1))
    return f'`{s}`'

if __name__ == '__main__':
    fname = sys.argv[1]
    with open(fname, 'rt') as md:
        for line in md:
            line = emph.sub(emph_repl, line)

            print(line, end='')

