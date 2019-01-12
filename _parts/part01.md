---
title: 01 - 简介与 REPL 解释器
date: 2019-01-12
---

作为一名 WEB 开发者，我几乎每天都在使用关系型数据库，但是它对于我来说就是一个黑盒子。我总是有如下疑问：

- 数据在磁盘和内存上的存储格式是怎样的？
- 何时将数据从内存写入磁盘？
- 为什么每个表只能有一个主键？
- 事务回滚是如何进行的？
- 数据库索引的格式是什么样的？
- 数据库何时会进行全表扫描？全表扫描是如何工作的？
- PREPARED 语句是以什么格式保存的？

简言之，就是数据库到底是如何**工作的**？

为了弄明白数据库是如何工作的，我以 sqlite 数据库为原型，从头实现了一个关系型数据库。Sqlite 数据库的特性相对于 MySQL 和 PostgreSQL 来说要少一些，但是我可以更好的理解数据库的原理。整个数据库的源码均存放在一个源文件中。

# Sqlite 数据库

Sqlite 官网有许多关于其[内部结构的文档](https://www.sqlite.org/arch.html)，同时，我也参考了 [SQLite Database System: Design and Implementation](https://books.google.com/books/about/SQLite_Database_System_Design_and_Implem.html?id=OEJ1CQAAQBAJ) 一书。

{% include image.html url="assets/images/arch1.gif" description="sqlite 数据库架构 (https://www.sqlite.org/zipvfs/doc/trunk/www/howitworks.wiki)" %}

为了获取查询结果或者修改数据，查询语句将经历一些列组件的处理。其中**前端 (front-end)** 主要由以下三个部分组成：

- tokenizer
- parser
- code generator

前端的输入是一个 SQL 查询语句，它的输出则是 sqlite 的虚拟机字节码（本质上就是一个已经编译好的可以被数据库执行的程序）。
