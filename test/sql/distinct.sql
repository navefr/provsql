\set ECHO none
SET search_path TO public,provsql;

CREATE TABLE distinct_result AS
  SELECT *, formula(provenance(),'personal_name')
  FROM (
    SELECT DISTINCT classification FROM personal
  ) t;

SELECT remove_provenance('distinct_result');
SELECT * FROM distinct_result ORDER BY classification;
DROP TABLE distinct_result;
