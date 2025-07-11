===================================
QED Image File Format Specification
===================================

The file format looks like this::

 +----------+----------+----------+-----+
 | cluster0 | cluster1 | cluster2 | ... |
 +----------+----------+----------+-----+

The first cluster begins with the ``header``. The header contains information
about where regular clusters start; this allows the header to be extensible and
store extra information about the image file. A regular cluster may be
a ``data cluster``, an ``L2``, or an ``L1 table``. L1 and L2 tables are composed
of one or more contiguous clusters.

Normally the file size will be a multiple of the cluster size.  If the file size
is not a multiple, extra information after the last cluster may not be preserved
if data is written. Legitimate extra information should use space between the header
and the first regular cluster.

All fields are little-endian.

Header
------

::

  Header {
     uint32_t magic;               /* QED\0 */

     uint32_t cluster_size;        /* in bytes */
     uint32_t table_size;          /* for L1 and L2 tables, in clusters */
     uint32_t header_size;         /* in clusters */

     uint64_t features;            /* format feature bits */
     uint64_t compat_features;     /* compat feature bits */
     uint64_t autoclear_features;  /* self-resetting feature bits */

     uint64_t l1_table_offset;     /* in bytes */
     uint64_t image_size;          /* total logical image size, in bytes */

     /* if (features & QED_F_BACKING_FILE) */
     uint32_t backing_filename_offset; /* in bytes from start of header */
     uint32_t backing_filename_size;   /* in bytes */
  }

Field descriptions:
~~~~~~~~~~~~~~~~~~~

- ``cluster_size`` must be a power of 2 in range [2^12, 2^26].
- ``table_size`` must be a power of 2 in range [1, 16].
- ``header_size`` is the number of clusters used by the header and any additional
  information stored before regular clusters.
- ``features``, ``compat_features``, and ``autoclear_features`` are file format
  extension bitmaps. They work as follows:

  - An image with unknown ``features`` bits enabled must not be opened. File format
    changes that are not backwards-compatible must use ``features`` bits.
  - An image with unknown ``compat_features`` bits enabled can be opened safely.
    The unknown features are simply ignored and represent backwards-compatible
    changes to the file format.
  - An image with unknown ``autoclear_features`` bits enable can be opened safely
    after clearing the unknown bits. This allows for backwards-compatible changes
    to the file format which degrade gracefully and can be re-enabled again by a
    new program later.
- ``l1_table_offset`` is the offset of the first byte of the L1 table in the image
  file and must be a multiple of ``cluster_size``.
- ``image_size`` is the block device size seen by the guest and must be a multiple
  of 512 bytes.
- ``backing_filename_offset`` and ``backing_filename_size`` describe a string in
  (byte offset, byte size) form. It is not NUL-terminated and has no alignment constraints.
  The string must be stored within the first ``header_size`` clusters. The backing filename
  may be an absolute path or relative to the image file.

Feature bits:
~~~~~~~~~~~~~

- ``QED_F_BACKING_FILE = 0x01``. The image uses a backing file.
- ``QED_F_NEED_CHECK = 0x02``. The image needs a consistency check before use.
- ``QED_F_BACKING_FORMAT_NO_PROBE = 0x04``. The backing file is a raw disk image
  and no file format autodetection should be attempted.  This should be used to
  ensure that raw backing files are never detected as an image format if they happen
  to contain magic constants.

There are currently no defined ``compat_features`` or ``autoclear_features`` bits.

Fields predicated on a feature bit are only used when that feature is set.
The fields always take up header space, regardless of whether or not the feature
bit is set.

Tables
------

Tables provide the translation from logical offsets in the block device to cluster
offsets in the file.

::

 #define TABLE_NOFFSETS (table_size * cluster_size / sizeof(uint64_t))

 Table {
     uint64_t offsets[TABLE_NOFFSETS];
 }

The tables are organized as follows::

                    +----------+
                    | L1 table |
                    +----------+
               ,------'  |  '------.
          +----------+   |    +----------+
          | L2 table |  ...   | L2 table |
          +----------+        +----------+
      ,------'  |  '------.
 +----------+   |    +----------+
 |   Data   |  ...   |   Data   |
 +----------+        +----------+

A table is made up of one or more contiguous clusters.  The ``table_size`` header
field determines table size for an image file. For example, ``cluster_size=64 KB``
and ``table_size=4`` results in 256 KB tables.

The logical image size must be less than or equal to the maximum possible size of
clusters rooted by the L1 table:

.. code::

 header.image_size <= TABLE_NOFFSETS * TABLE_NOFFSETS * header.cluster_size

L1, L2, and data cluster offsets must be aligned to ``header.cluster_size``.
The following offsets have special meanings:

L2 table offsets
~~~~~~~~~~~~~~~~

- 0 - unallocated. The L2 table is not yet allocated.

Data cluster offsets
~~~~~~~~~~~~~~~~~~~~

- 0 - unallocated.  The data cluster is not yet allocated.
- 1 - zero. The data cluster contents are all zeroes and no cluster is allocated.

Future format extensions may wish to store per-offset information. The least
significant 12 bits of an offset are reserved for this purpose and must be set
to zero. Image files with ``cluster_size`` > 2^12 will have more unused bits
which should also be zeroed.

Unallocated L2 tables and data clusters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Reads to an unallocated area of the image file access the backing file. If there
is no backing file, then zeroes are produced. The backing file may be smaller
than the image file and reads of unallocated areas beyond the end of the backing
file produce zeroes.

Writes to an unallocated area cause a new data clusters to be allocated, and a new
L2 table if that is also unallocated. The new data cluster is populated with data
from the backing file (or zeroes if no backing file) and the data being written.

Zero data clusters
~~~~~~~~~~~~~~~~~~

Zero data clusters are a space-efficient way of storing zeroed regions of the image.

Reads to a zero data cluster produce zeroes.

.. note::
    The difference between an unallocated and a zero data cluster is that zero data
    clusters stop the reading of contents from the backing file.

Writes to a zero data cluster cause a new data cluster to be allocated.  The new
data cluster is populated with zeroes and the data being written.

Logical offset translation
~~~~~~~~~~~~~~~~~~~~~~~~~~

Logical offsets are translated into cluster offsets as follows::

  table_bits table_bits    cluster_bits
  <--------> <--------> <--------------->
 +----------+----------+-----------------+
 | L1 index | L2 index |     byte offset |
 +----------+----------+-----------------+

       Structure of a logical offset

 offset_mask = ~(cluster_size - 1) # mask for the image file byte offset

 def logical_to_cluster_offset(l1_index, l2_index, byte_offset):
   l2_offset = l1_table[l1_index]
   l2_table = load_table(l2_offset)
   cluster_offset = l2_table[l2_index] & offset_mask
   return cluster_offset + byte_offset

Consistency checking
--------------------

This section is informational and included to provide background on the use
of the ``QED_F_NEED_CHECK features`` bit.

The ``QED_F_NEED_CHECK`` bit is used to mark an image as dirty before starting
an operation that could leave the image in an inconsistent state if interrupted
by a crash or power failure.  A dirty image must be checked on open because its
metadata may not be consistent.

Consistency check includes the following invariants:

- Each cluster is referenced once and only once. It is an inconsistency to have
  a cluster referenced more than once by L1 or L2 tables. A cluster has been leaked
  if it has no references.
- Offsets must be within the image file size and must be ``cluster_size`` aligned.
- Table offsets must at least ``table_size`` * ``cluster_size`` bytes from the end
  of the image file so that there is space for the entire table.

The consistency check process starts from ``l1_table_offset`` and scans all L2 tables.
After the check completes with no other errors besides leaks, the ``QED_F_NEED_CHECK``
bit can be cleared and the image can be accessed.
