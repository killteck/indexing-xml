
CREATE TYPE numrange AS RANGE (
  SUBTYPE = NUMERIC,
  SUBTYPE_CMP = numeric_cmp
);

CREATE TABLE numrange_test (nr NUMRANGE);

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

select range(2.0, 3.0) -|- range(3.0, 4.0);
select adjacent(range(2.0, 3.0), range(3.1, 4.0));
select rangeii(2.0, 3.0) -|- range__(3.0, 4.0);
select range(1.0, 2.0) -|- rangeii(2.0, 3.0);
select adjacent(range_i(2.0, 3.0), range_i(1.0, 2.0));

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
select range(1.1, 2.2) < range__(1.1, -100.1);

DROP TABLE numrange_test;
