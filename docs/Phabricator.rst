=============================
Code Reviews with Phabricator
=============================

.. contents::
  :local:

If you prefer to use a web user interface for code reviews, you can now submit
your patches for Clang and LLVM at `LLVM's Phabricator`_ instance.

While Phabricator is a useful tool for some, the relevant -commits mailing list
is the system of record for all LLVM code review. The mailing list should be
added as a subscriber on all reviews, and Phabricator users should be prepared
to respond to free-form comments in mail sent to the commits list.

Sign up
-------

To get started with Phabricator, navigate to `http://reviews.llvm.org`_ and
click the power icon in the top right. You can register with a GitHub account,
a Google account, or you can create your own profile.

Make *sure* that the email address registered with Phabricator is subscribed
to the relevant -commits mailing list. If your are not subscribed to the commit
list, all mail sent by Phabricator on your behalf will be held for moderation.

Note that if you use your Subversion user name as Phabricator user name,
Phabricator will automatically connect your submits to your Phabricator user in
the `Code Repository Browser`_.

Requesting a review via the command line
----------------------------------------

Phabricator has a tool called *Arcanist* to upload patches from
the command line. To get you set up, follow the
`Arcanist Quick Start`_ instructions.

You can learn more about how to use arc to interact with
Phabricator in the `Arcanist User Guide`_.

Requesting a review via the web interface
-----------------------------------------

The tool to create and review patches in Phabricator is called
*Differential*.

Note that you can upload patches created through various diff tools,
including git and svn. To make reviews easier, please always include
**as much context as possible** with your diff! Don't worry, Phabricator
will automatically send a diff with a smaller context in the review
email, but having the full file in the web interface will help the
reviewer understand your code.

To get a full diff, use one of the following commands (or just use Arcanist
to upload your patch):

* ``git diff -U999999 other-branch``
* ``svn diff --diff-cmd=diff -x -U999999``

To upload a new patch:

* Click *Differential*.
* Click *Create Diff*.
* Paste the text diff or upload the patch file.
  Note that TODO
* Leave the drop down on *Create a new Revision...* and click *Continue*.
* Enter a descriptive title and summary; add reviewers and mailing
  lists that you want to be included in the review. If your patch is
  for LLVM, add llvm-commits as a subscriber; if your patch is for Clang,
  add cfe-commits.
* Click *Save*.

To submit an updated patch:

* Click *Differential*.
* Click *Create Diff*.
* Paste the updated diff.
* Select the review you want to from the *Attach To* dropdown and click
  *Continue*.
* Click *Save*.

Reviewing code with Phabricator
-------------------------------

Phabricator allows you to add inline comments as well as overall comments
to a revision. To add an inline comment, select the lines of code you want
to comment on by clicking and dragging the line numbers in the diff pane.

You can add overall comments or submit your comments at the bottom of the page.

Phabricator has many useful features, for example allowing you to select
diffs between different versions of the patch as it was reviewed in the
*Revision Update History*. Most features are self descriptive - explore, and
if you have a question, drop by on #llvm in IRC to get help.

Note that as e-mail is the system of reference for code reviews, and some
people prefer it over a web interface, we do not generate automated mail
when a review changes state, for example by clicking "Accept Revision" in
the web interface. Thus, please type LGTM into the comment box to accept
a change from Phabricator.

Committing a change
-------------------

Arcanist can manage the commit transparently. It will retrieve the description,
reviewers, the ``Differential Revision``, etc from the review and commit it to the repository.

::

  arc patch D<Revision>
  arc commit --revision D<Revision>


When committing an LLVM change that has been reviewed using
Phabricator, the convention is for the commit message to end with the
line:

::

  Differential Revision: <URL>

where ``<URL>`` is the URL for the code review, starting with
``http://reviews.llvm.org/``.

Note that Arcanist will add this automatically.

This allows people reading the version history to see the review for
context.  This also allows Phabricator to detect the commit, close the
review, and add a link from the review to the commit.

Status
------

Please let us know whether you like it and what could be improved!

.. _LLVM's Phabricator: http://reviews.llvm.org
.. _`http://reviews.llvm.org`: http://reviews.llvm.org
.. _Code Repository Browser: http://reviews.llvm.org/diffusion/
.. _Arcanist Quick Start: http://www.phabricator.com/docs/phabricator/article/Arcanist_Quick_Start.html
.. _Arcanist User Guide: http://www.phabricator.com/docs/phabricator/article/Arcanist_User_Guide.html
