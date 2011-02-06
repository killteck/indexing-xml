
CREATE TABLE numrange_test (nr NUMRANGE);
create index numrange_test_btree on numrange_test(nr);
SET enable_seqscan = f;

-- negative test; should fail
INSERT INTO numrange_test VALUES('-[1.1, 2.2)');
INSERT INTO numrange_test VALUES('[1.1, NULL)');
INSERT INTO numrange_test VALUES('[NULL, 2.2)');
INSERT INTO numrange_test VALUES('[NULL, NULL)');

-- should succeed
INSERT INTO numrange_test VALUES('[-INF, INF)');
INSERT INTO numrange_test VALUES('[3, INF]');
INSERT INTO numrange_test VALUES('[-INF, 5)');
INSERT INTO numrange_test VALUES(range(1.1, 2.2));
INSERT INTO numrange_test VALUES('-');
INSERT INTO numrange_test VALUES(range(1.7));

SELECT empty(nr) FROM numrange_test;
SELECT lower_inc(nr), lower(nr), upper(nr), upper_inc(nr) FROM numrange_test
  WHERE NOT empty(nr) AND NOT lower_inf(nr) AND NOT upper_inf(nr);

SELECT * FROM numrange_test WHERE contains(nr, range(1.9,1.91));
SELECT * FROM numrange_test WHERE nr @> range(1.0,10000.1);
SELECT * FROM numrange_test WHERE contained_by(range(-1e7,-10000.1), nr);
SELECT * FROM numrange_test WHERE 1.9 <@ nr;
SELECT * FROM numrange_test WHERE nr = '-';
SELECT * FROM numrange_test WHERE range_eq(nr, '(1.1, 2.2)');
SELECT * FROM numrange_test WHERE nr = '[1.1, 2.2)';

select range(2.0, 1.0);

select range(2.0, 3.0) -|- range(3.0, 4.0);
select adjacent(range(2.0, 3.0), range(3.1, 4.0));
select rangeii(2.0, 3.0) -|- range__(3.0, 4.0);
select range(1.0, 2.0) -|- rangeii(2.0, 3.0);
select adjacent(range_i(2.0, 3.0), range_i(1.0, 2.0));

select range(1.1, 3.3) <@ range(0.1,10.1);
select range(0.1, 10.1) <@ range(1.1,3.3);

select range(1.1, 2.2) - range(2.0, 3.0);
select range(1.1, 2.2) - range(2.2, 3.0);
select rangeii(1.1, 2.2) - range(2.0, 3.0);
select minus(rangeii(10.1, 12.2), range_i(110.0,120.2));
select minus(rangeii(10.1, 12.2), range_i(0.0,120.2));

select rangeii(4.5, 5.5) && range(5.5, 6.5);
select range(1.0, 2.0) << range(3.0, 4.0);
select range(1.0, 2.0) >> range(3.0, 4.0);
select range(3.0, 70.0) &< range(6.6, 100.0);

select range(1.1, 2.2) < range(1.0, 200.2);
select range(1.1, 2.2) < range__(1.1, 1.2);

select range(1.0, 2.0) + range(2.0, 3.0);
select range(1.0, 2.0) + range(1.5, 3.0);
select range(1.0, 2.0) + range(2.5, 3.0);

select range(1.0, 2.0) * range(2.0, 3.0);
select range(1.0, 2.0) * range(1.5, 3.0);
select range(1.0, 2.0) * range(2.5, 3.0);

select * from numrange_test where nr < rangeii(-1000.0, -1000.0);
select * from numrange_test where nr < rangeii(0.0, 1.0);
select * from numrange_test where nr < rangeii(1000.0, 1001.0);
select * from numrange_test where nr > rangeii(-1001.0, -1000.0);
select * from numrange_test where nr > rangeii(0.0, 1.0);
select * from numrange_test where nr > rangeii(1000.0, 1000.0);

create table numrange_test2(nr numrange);
create index numrange_test2_hash_idx on numrange_test2 (nr);
INSERT INTO numrange_test2 VALUES('[-INF, 5)');
INSERT INTO numrange_test2 VALUES(range(1.1, 2.2));
INSERT INTO numrange_test2 VALUES(range(1.1, 2.2));
INSERT INTO numrange_test2 VALUES(range__(1.1, 2.2));
INSERT INTO numrange_test2 VALUES('-');

select * from numrange_test2 where nr = '-'::numrange;
select * from numrange_test2 where nr = range(1.1, 2.2);
select * from numrange_test2 where nr = range(1.1, 2.3);

set enable_nestloop=t;
set enable_hashjoin=f;
set enable_mergejoin=f;
select * from numrange_test natural join numrange_test2 order by nr;
set enable_nestloop=f;
set enable_hashjoin=t;
set enable_mergejoin=f;
select * from numrange_test natural join numrange_test2 order by nr;
set enable_nestloop=f;
set enable_hashjoin=f;
set enable_mergejoin=t;
select * from numrange_test natural join numrange_test2 order by nr;

set enable_nestloop to default;
set enable_hashjoin to default;
set enable_mergejoin to default;
SET enable_seqscan TO DEFAULT;
DROP TABLE numrange_test;
DROP TABLE numrange_test2;

-- test canonical form for int4range
select range__(1,10);
select rangei_(1,10);
select range_i(1,10);
select rangeii(1,10);

-- test canonical form for daterange
select range__('2000-01-10'::date, '2000-01-20'::date);
select rangei_('2000-01-10'::date, '2000-01-20'::date);
select range_i('2000-01-10'::date, '2000-01-20'::date);
select rangeii('2000-01-10'::date, '2000-01-20'::date);

-- test length()
select length(range(10.1,100.1));
select length('[2000-01-01 01:00:00, 2000-01-05 03:00:00)'::tsrange);
select length('[2000-01-01 01:00:00, 2000-01-01 03:00:00)'::tstzrange);
select length('[2000-01-01, 2000-01-05)'::daterange);

create table test_range_gist(ir int4range);
create index test_range_gist_idx on test_range_gist using gist (ir);

insert into test_range_gist select range(g, g+10) from generate_series(1,2000) g;
insert into test_range_gist select '-'::int4range from generate_series(1,500) g;
insert into test_range_gist select range(g, g+10000) from generate_series(1,1000) g;
insert into test_range_gist select '-'::int4range from generate_series(1,500) g;
insert into test_range_gist select range_linfi(g*10) from generate_series(1,100) g;
insert into test_range_gist select range_uinf_(g*10) from generate_series(1,100) g;
insert into test_range_gist select range(g, g+10) from generate_series(1,2000) g;

BEGIN;
SET LOCAL enable_seqscan    = t;
SET LOCAL enable_bitmapscan = f;
SET LOCAL enable_indexscan  = f;

select count(*) from test_range_gist where ir @> '-'::int4range;
select count(*) from test_range_gist where ir = range(10,20);
select count(*) from test_range_gist where ir @> 10;
select count(*) from test_range_gist where ir @> range(10,20);
select count(*) from test_range_gist where ir && range(10,20);
select count(*) from test_range_gist where ir <@ range(10,50);
select count(*) from (select * from test_range_gist where ir?) s where ir << range(100,500);
select count(*) from (select * from test_range_gist where ir?) s where ir >> range(100,500);
select count(*) from (select * from test_range_gist where ir?) s where ir &< range(100,500);
select count(*) from (select * from test_range_gist where ir?) s where ir &> range(100,500);
select count(*) from (select * from test_range_gist where ir?) s where ir -|- range(100,500);
COMMIT;

BEGIN;
SET LOCAL enable_seqscan    = f;
SET LOCAL enable_bitmapscan = f;
SET LOCAL enable_indexscan  = t;

select count(*) from test_range_gist where ir @> '-'::int4range;
select count(*) from test_range_gist where ir = range(10,20);
select count(*) from test_range_gist where ir @> 10;
select count(*) from test_range_gist where ir @> range(10,20);
select count(*) from test_range_gist where ir && range(10,20);
select count(*) from test_range_gist where ir <@ range(10,50);
select count(*) from test_range_gist where ir << range(100,500);
select count(*) from test_range_gist where ir >> range(100,500);
select count(*) from test_range_gist where ir &< range(100,500);
select count(*) from test_range_gist where ir &> range(100,500);
select count(*) from test_range_gist where ir -|- range(100,500);
COMMIT;

drop index test_range_gist_idx;
create index test_range_gist_idx on test_range_gist using gist (ir);

BEGIN;
SET LOCAL enable_seqscan    = f;
SET LOCAL enable_bitmapscan = f;
SET LOCAL enable_indexscan  = t;

select count(*) from test_range_gist where ir @> '-'::int4range;
select count(*) from test_range_gist where ir = range(10,20);
select count(*) from test_range_gist where ir @> 10;
select count(*) from test_range_gist where ir @> range(10,20);
select count(*) from test_range_gist where ir && range(10,20);
select count(*) from test_range_gist where ir <@ range(10,50);
select count(*) from test_range_gist where ir << range(100,500);
select count(*) from test_range_gist where ir >> range(100,500);
select count(*) from test_range_gist where ir &< range(100,500);
select count(*) from test_range_gist where ir &> range(100,500);
select count(*) from test_range_gist where ir -|- range(100,500);
COMMIT;

drop table test_range_gist;

--
-- Btree_gist is not included by default, so to test exclusion
-- constraints with range types, use singleton int ranges for the "="
-- portion of the constraint.
--

create table test_range_excl(
  room int4range,
  speaker int4range,
  during tsrange,
  exclude using gist (room with =, during with &&),
  exclude using gist (speaker with =, during with &&)
);

insert into test_range_excl
  values(range(123), range(1), '[2010-01-02 10:00, 2010-01-02 11:00)');
insert into test_range_excl
  values(range(123), range(2), '[2010-01-02 11:00, 2010-01-02 12:00)');
insert into test_range_excl
  values(range(123), range(3), '[2010-01-02 10:10, 2010-01-02 11:10)');
insert into test_range_excl
  values(range(124), range(3), '[2010-01-02 10:10, 2010-01-02 11:10)');
insert into test_range_excl
  values(range(125), range(1), '[2010-01-02 10:10, 2010-01-02 11:10)');

drop table test_range_excl;

-- test bigint ranges
select range_i(10000000000::int8, 20000000000::int8);
-- test tstz ranges
set timezone to '-08';
select '[2010-01-01 01:00:00 -05, 2010-01-01 02:00:00 -08)'::tstzrange;
-- should fail
select '[2010-01-01 01:00:00 -08, 2010-01-01 02:00:00 -05)'::tstzrange;
set timezone to default;
