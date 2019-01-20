TEMPLATE = subdirs

CONFIG += ordered

SUBDIRS = breakpad

win32: SUBDIRS += crashserver crashreporter

