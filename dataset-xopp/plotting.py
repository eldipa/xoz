import seaborn as sns
#sns.set_context(rc={'lines.markeredgewidth': 0.1})

import matplotlib
import matplotlib.pyplot as plt

from math import sqrt
import contextlib

# From # https://nipunbatra.github.io/blog/visualisation/2014/06/02/latexify.html
# and adapted to work with Seaborn
LATEXIFY_KARGS = {'fig_width', 'fig_height', 'columns', 'usetex', 'rc'}
def latexify(fig_width=None, fig_height=None, columns=0, usetex=True, rc={}):
    """Set up matplotlib's RC params for LaTeX plotting.
    Call this before plotting a figure.

    Parameters
    ----------
    fig_width : float, optional, inches
    fig_height : float,  optional, inches
    columns : {0, 1, 2}
    """

    params = dict(rc)
    params.update({
              'backend': 'ps',
              'text.latex.preamble': r'\usepackage{gensymb}',
              'axes.labelsize':  8, # fontsize for x and y labels (was 10)
              'axes.titlesize':  8,
              'legend.fontsize': 8, # was 10
              'xtick.labelsize': 8,
              'ytick.labelsize': 8,
              'text.usetex': usetex,
              'font.family': 'serif',
              'font.size':       8, # was 10
              'lines.markersize': 3,
    })

    assert(columns in [0,1,2])
    if columns != 0:
        # Code adapted from http://www.scipy.org/Cookbook/Matplotlib/LaTeX_Examples

        # Width and max height in inches for IEEE journals taken from
        # computer.org/cms/Computer.org/Journal%20templates/transactions_art_guide.pdf
        if fig_width is None:
            fig_width = 3.39 if columns==1 else 6.9 # width in inches

        if fig_height is None:
            golden_mean = (sqrt(5)-1.0)/2.0    # Aesthetic ratio
            fig_height = fig_width*golden_mean # height in inches

        MAX_HEIGHT_INCHES = 8.0
        if fig_height > MAX_HEIGHT_INCHES:
            print("WARNING: fig_height too large: %4.f so will reduce to %4.f inches" % (fig_height, MAX_HEIGHT_INCHES))
            fig_height = MAX_HEIGHT_INCHES

        params['figure.figsize'] = [fig_width,fig_height]
    return params

# From matplotlib documentation
SAVEFIG_KARGS = {
        'dpi',
        'facecolor',
        'edgecolor',
        'orientation',
        'papertype',
        'format',
        'bbox_inches',
        'pad_inches',
        'frameon',
        'metadata',

        # 'transparent',  on SVG files this does not work well,
        # it is better to use facecolor = (0, 0, 0, 0)
        # See the 'transparent' shortcut on show() about this
        }

# https://stackoverflow.com/questions/14827650/pyplot-scatter-plot-marker-size
_inside_of_a_show_and_save_context = False

@contextlib.contextmanager
def show(save=None, *, skip=False, context='paper', style='darkgrid', transparent=True, **kargs):
    global _inside_of_a_show_and_save_context

    if skip:
        yield
        return

    is_svg = save is not None and save.endswith('.svg')

    savefig_kargs = dict(bbox_inches='tight', dpi=600)
    savefig_kargs.update({k:v for k,v in kargs.items() if k in SAVEFIG_KARGS})

    latexify_kargs = {k:v for k,v in kargs.items() if k in LATEXIFY_KARGS}

    if transparent and 'facecolor' in savefig_kargs and is_svg:
        raise Exception(f"You cannot mix 'transparent' with 'facecolor' in a SVG. Sorry.")

    if transparent and is_svg:
        savefig_kargs['facecolor'] = (0,0,0,0)

    unused_kargs = set(kargs.keys()) - SAVEFIG_KARGS - LATEXIFY_KARGS
    if unused_kargs:
        raise TypeError(f"Unexpected keyword arguments: {unused_kargs}")

    are_we_the_outer_context = False
    if not _inside_of_a_show_and_save_context:
        plt.close()

        _inside_of_a_show_and_save_context = True
        are_we_the_outer_context = True

        if not save and 'usetex' not in latexify_kargs:
            latexify_kargs = latexify_kargs.copy()
            latexify_kargs['usetex'] = False # disable latex, it is faster

        sns.set(context, style, rc=latexify(**latexify_kargs))

    yield

    if are_we_the_outer_context:
        _inside_of_a_show_and_save_context = False

        plt.tight_layout()
        if save:
            plt.savefig(save, **savefig_kargs)

        plt.show()
        sns.set() # reset to seaborn's default

if __name__ == '__main__':
    import numpy as np
    import matplotlib.pyplot as plt

    with show('test-plt.svg'):
        x1 = np.linspace(0.0, 5.0)
        x2 = np.linspace(0.0, 2.0)

        y1 = np.cos(2 * np.pi * x1) * np.exp(-x1)
        y2 = np.cos(2 * np.pi * x2)

        fig, (ax1, ax2) = plt.subplots(2, 1)
        fig.suptitle('A tale of 2 subplots')

        ax1.plot(x1, y1, 'o-')
        ax1.set_ylabel('Damped oscillation')

        ax2.plot(x2, y2, '.-')
        ax2.set_xlabel('time (s)')
        ax2.set_ylabel('Undamped')

