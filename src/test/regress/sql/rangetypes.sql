
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
SELECT * FROM numrange_test WHERE contains(nr, range(1.0,10000.1));
SELECT * FROM numrange_test WHERE contained_by(range(-1e7,-10000.1), nr);
SELECT * FROM numrange_test WHERE contains(nr, 1.9);
SELECT * FROM numrange_test WHERE range_eq(nr, '-');
SELECT * FROM numrange_test WHERE range_eq(nr, '(1.1, 2.2)');
SELECT * FROM numrange_test WHERE range_eq(nr, '[1.1, 2.2)');

DROP TABLE numrange_test;
