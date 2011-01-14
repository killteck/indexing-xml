
CREATE TYPE numrange AS RANGE (
  SUBTYPE = NUMERIC,
  SUBTYPE_CMP = numeric_cmp
);

CREATE TABLE numrange_test (nr NUMRANGE);

INSERT INTO numrange_test VALUES(range(1.1, 2.2));
INSERT INTO numrange_test VALUES('-');
INSERT INTO numrange_test VALUES('-[1.1, 2.2)');
INSERT INTO numrange_test VALUES('[-INF, INF)');
INSERT INTO numrange_test VALUES('[1.1, NULL)');
INSERT INTO numrange_test VALUES('[NULL, 2.2)');

SELECT * FROM numrange_test WHERE contains(nr, range(1.9,1.91));
SELECT * FROM numrange_test WHERE contains(nr, 1.9);
SELECT * FROM numrange_test WHERE range_eq(nr, '-');
SELECT * FROM numrange_test WHERE range_eq(nr, '[NULL, 2.2)');
SELECT * FROM numrange_test WHERE range_eq(nr, '(1.1, 2.2)');
SELECT * FROM numrange_test WHERE range_eq(nr, '[1.1, 2.2)');

DROP TABLE numrange_test;
