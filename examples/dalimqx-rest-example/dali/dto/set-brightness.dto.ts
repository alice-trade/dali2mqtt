import { IsInt, Min, Max } from 'class-validator';

export class SetBrightnessDto {
    @IsInt()
    @Min(0)
    @Max(254)
    level: number;
}